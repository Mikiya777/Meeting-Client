#include "network/include/HttpClient.h"

#include "log/include/Logger.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace {

void Expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "[FAILED] " << message << '\n';
    std::exit(1);
  }
  std::cout << "[PASSED] " << message << '\n';
}

std::string ReadHttpRequest(int client_fd) {
  std::string request;
  char buffer[1024];
  while (request.find("\r\n\r\n") == std::string::npos) {
    const ssize_t count = recv(client_fd, buffer, sizeof(buffer), 0);
    if (count <= 0) {
      return request;
    }
    request.append(buffer, static_cast<size_t>(count));
  }

  const size_t header_end = request.find("\r\n\r\n") + 4;
  const std::string headers = request.substr(0, header_end);
  const std::string key = "Content-Length:";
  const size_t position = headers.find(key);
  if (position == std::string::npos) {
    return request;
  }

  std::istringstream stream(headers.substr(position + key.size()));
  size_t content_length = 0;
  stream >> content_length;
  while (request.size() < header_end + content_length) {
    const ssize_t count = recv(client_fd, buffer, sizeof(buffer), 0);
    if (count <= 0) {
      break;
    }
    request.append(buffer, static_cast<size_t>(count));
  }
  return request;
}

void WriteHttpResponse(int client_fd,
                       int status_code,
                       const std::string &reason,
                       const std::string &body,
                       const std::string &header_value) {
  std::ostringstream response;
  response << "HTTP/1.1 " << status_code << ' ' << reason << "\r\n"
           << "Content-Type: text/plain\r\n"
           << "X-Test-Header: " << header_value << "\r\n"
           << "Content-Length: " << body.size() << "\r\n"
           << "Connection: close\r\n\r\n"
           << body;
  const std::string text = response.str();
  send(client_fd, text.data(), text.size(), 0);
}

class LocalHttpServer {
public:
  LocalHttpServer() {
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    Expect(server_fd_ >= 0, "local http server socket is created");

    int reuse = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    address.sin_port = 0;
    Expect(bind(server_fd_, reinterpret_cast<sockaddr *>(&address),
                sizeof(address)) == 0,
           "local http server bind succeeds");
    Expect(listen(server_fd_, 3) == 0, "local http server listens");

    socklen_t length = sizeof(address);
    Expect(getsockname(server_fd_, reinterpret_cast<sockaddr *>(&address),
                       &length) == 0,
           "local http server port is assigned");
    port_ = ntohs(address.sin_port);

    worker_ = std::thread([this] {
      for (int i = 0; i < 3; ++i) {
        const int client_fd = accept(server_fd_, nullptr, nullptr);
        if (client_fd < 0) {
          continue;
        }

        const std::string request = ReadHttpRequest(client_fd);
        if (request.rfind("GET /get ", 0) == 0) {
          WriteHttpResponse(client_fd, 200, "OK", "get-ok", "get");
        } else if (request.rfind("POST /post ", 0) == 0 &&
                   request.find("hello=world") != std::string::npos) {
          WriteHttpResponse(client_fd, 201, "Created", "post-ok", "post");
        } else if (request.rfind("DELETE /delete ", 0) == 0) {
          WriteHttpResponse(client_fd, 200, "OK", "delete-ok", "delete");
        } else {
          WriteHttpResponse(client_fd, 400, "Bad Request", "bad-request", "bad");
        }
        close(client_fd);
      }
    });
  }

  ~LocalHttpServer() {
    if (worker_.joinable()) {
      worker_.join();
    }
    if (server_fd_ >= 0) {
      close(server_fd_);
    }
  }

  std::string Url(const std::string &path) const {
    return "http://127.0.0.1:" + std::to_string(port_) + path;
  }

private:
  int server_fd_ = -1;
  uint16_t port_ = 0;
  std::thread worker_;
};

} // namespace

int main() {
  LOGGER::Logger::Instance().Initialize(LOGGER::LogOutput::kConsole,
                                        LOGGER::LogLevel::kWarn);

  LocalHttpServer server;
  HTTP::Client client;

  const auto get = client.GetRequest(server.Url("/get"));
  Expect(get.status_code == 200, "GET status code is captured");
  Expect(get.body == "get-ok", "GET body is captured");
  Expect(get.headers.at("x-test-header") == "get", "GET header is captured");

  const auto post = client.PostRequest(
      server.Url("/post"), {{"Content-Type", "application/x-www-form-urlencoded"}},
      "hello=world");
  Expect(post.status_code == 201, "POST status code is captured");
  Expect(post.body == "post-ok", "POST body is captured");

  const auto del = client.DeleteRequest(server.Url("/delete"));
  Expect(del.status_code == 200, "DELETE status code is captured");
  Expect(del.body == "delete-ok", "DELETE body is captured");

  LOGGER::Logger::Instance().Shutdown();
  return 0;
}
