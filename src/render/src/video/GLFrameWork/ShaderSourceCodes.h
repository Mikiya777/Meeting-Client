#ifndef SHADER_SOURCE_CODES_H
#define SHADER_SOURCE_CODES_H
/*
把GLSL shader源码作为字符串常量放在c++里面。

VideoRender
  -> VideoShader
      -> ShaderSourceCodes.h

上游：
VideoShader::Initialize()

下游：
OpenGL shader compiler
*/

//std::string_view 是一种轻量字符串视图，不负责拥有内存，只是看一段字符串内容。
#include <string_view>

namespace RENDER {

/*
编写Vertex Shader的程序供GPU执行。

#version 330 core ：使用 OpenGL 3.3 core profile 的 GLSL 语法
之后的三行是在规定输入和输出，
layout (location = 0) in vec2 aPos; 指的是 location 0 输入两个float作为 aPos
layout (location = 1) in vec2 aTexCoord; 指的是 location 1 输入两个float作为 aTexCoord

vertex shader 有两个输出：（几何坐标与纹理坐标成对出现）
1. gl_Position：内置输出，告诉 OpenGL 这个顶点的位置
2. TexCoord：是一个像素的坐标（纹理坐标），把纹理坐标传给 fragment shader

知识点：
1.gl_Position 是四维的 x y z w 这样方便进行一些三维计算如平移 透视投影等 视频渲染项目里虽用不到但得保证格式要求
2.aTexCoord 先在 vertex shader 中赋值给 out 变量 TexCoord；OpenGL 在光栅化阶段会对三角形内部的 TexCoord 自动插值；fragment shader 收到的是插值后的 TexCoord。
*/
inline constexpr std::string_view kI420VertexShaderSource = R"(
#version 330 core 
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;
out vec2 TexCoord;

void main()
{
    gl_Position = vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

/*
编写Fragment Shader的程序供GPU执行。
接受 2个float长度的TexCoord
输出 4个float长度的FragColor

声明三个纹理采样器
uniform sampler2D yTexture;
uniform sampler2D uTexture;
uniform sampler2D vTexture;

注：
1.uniform是这个变量由c++设置，在一次绘制过程中对所有fragment都一样，sampler2D 本身不装图像数据，它装的是“去哪一个 texture unit 找图像”的编号，编号位置由CPU设置。
2.texture() 函数会根据 sampler2D 记录的 texture unit 编号找到对应纹理，再根据 TexCoord 指定的纹理坐标位置进行采样，返回该位置的纹理值。

main()函数里处理
1.使用GLSL 内置采样函数采样Y/U/V 用纹理坐标TexCoord去yTexture 对应的纹理中采样，取采样结果的r通道，作为y值，U/V同理。
2.处理U/V偏移：U/V围绕中间值表示无偏移，U = 0.5 表示没有蓝色色差，V = 0.5 表示没有红色色差 shader希望 U/V围绕0，所以要做一个对齐。
3.Y/U/V->RGB： 通过公式转换 系数为BT.709标准
4.输出颜色：OpenGL 颜色输出一般希望在：0.0～1.0（标准化区间）clamp(r, 0.0, 1.0)表示向中间截断，超出[0,1]范围内的 <0 截断成0， >1截断成1。
输出颜色格式为：r g b alpha，alpha表示透明度。

注：fragment shader 是对每个 fragment 执行一次，所以这里的 YUV -> RGB 是逐像素并行完成的。

过程：
数据线：
Y 平面数据 -> m_y_texture -> texture unit 0

指令线：
fragment shader 里的 yTexture sampler -> 被 CPU 设置为 0 -> 指向 texture unit 0

位置线：
当前 fragment -> TexCoord -> 决定取纹理上的哪个位置
*/
inline constexpr std::string_view kI420FragmentShaderSource = R"(
#version 330 core
in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D yTexture;
uniform sampler2D uTexture;
uniform sampler2D vTexture;

void main()
{
    float y = texture(yTexture, TexCoord).r;
    float u = texture(uTexture, TexCoord).r - 0.5;
    float v = texture(vTexture, TexCoord).r - 0.5;

    float r = y + 1.5748 * v;
    float g = y - 0.1873 * u - 0.4681 * v;
    float b = y + 1.8556 * u;

    FragColor = vec4(clamp(r, 0.0, 1.0),
                     clamp(g, 0.0, 1.0),
                     clamp(b, 0.0, 1.0),
                     1.0);
}
)";

} // namespace RENDER

#endif // SHADER_SOURCE_CODES_H
