#include "bvh.hpp"

#include <GL/glew.h>
#include <SDL.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/mesh.h>
#include <assimp/postprocess.h>

#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

class Render {
private:
	std::uint32_t m_width;
	std::uint32_t m_height;

    float m_timer;

	GLuint m_fbo;
	GLuint m_fboTexture;

	GLuint m_shader;
	GLuint m_program;

    Assimp::Importer m_meshImporter;

    std::vector<float> m_geometryPos;
    GLuint m_ssboGeometryPos;

    std::vector<float> m_geometryNormal;
    GLuint m_ssboGeometryNormal;

    std::vector<float> m_geometryColor;
    GLuint m_ssboGeometryColor;

    std::vector<bool> m_bvhLeaf;
    GLuint m_ssboBvhLeaf;

    std::vector<float> m_bvhAABBMin;
    GLuint m_ssboBvhAABBMin;

    std::vector<float> m_bvhAABBMax;
    GLuint m_ssboBvhAABBMax;

    std::vector<std::uint32_t> m_bvhChild;
    GLuint m_ssboBvhChild;

    std::vector<std::uint32_t> m_bvhPrimitive;
    GLuint m_ssboBvhPrimitive;

    BVH m_bvh;
    glm::mat4 m_viewInv;

public:
	Render(std::uint32_t width, std::uint32_t height) :
		m_width(width),
        m_height(height),
        m_timer(0.0f),
        m_viewInv(glm::inverse(glm::lookAt(glm::vec3(0.0f, 10.0f, 50.0f), glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f))))
    {
        glewInit();

		glGenTextures(1, &m_fboTexture);
		glBindTexture(GL_TEXTURE_2D, m_fboTexture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		glGenFramebuffers(1, &m_fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
		glViewport(0, 0, m_width, m_height);
		GLenum drawBuffers[1] = {GL_COLOR_ATTACHMENT0};
		glDrawBuffers(1, drawBuffers);
		glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, m_fboTexture, 0);

		int errLen = 0;
		char buffer[1024] = {};
		const char* sourcePtr = nullptr;
		std::string shaderSource;
		{
			std::ifstream input("compute.glsl");
			input.seekg(0, std::ios::end);
			std::uint32_t size = input.tellg();
			input.seekg(0, std::ios::beg);
            shaderSource.resize(size, 0);
			input.read(&shaderSource[0], size);
		}

		sourcePtr = shaderSource.c_str();
		m_shader = glCreateShader(GL_COMPUTE_SHADER);
		glShaderSource(m_shader, 1, &sourcePtr, nullptr);
		glCompileShader(m_shader);
		glGetShaderInfoLog(m_shader, sizeof(buffer), &errLen, buffer);
		if (errLen > 0) std::cout << std::string(buffer, buffer + errLen) << std::endl;

		m_program = glCreateProgram();
		glAttachShader(m_program, m_shader);
		glLinkProgram(m_program);
		glGetProgramInfoLog(m_program, sizeof(buffer), &errLen, buffer);
		if (errLen > 0) std::cout << std::string(buffer, buffer + errLen) << std::endl;

        const aiScene* scene = m_meshImporter.ReadFile("sponza.obj", aiProcess_Triangulate);
        for (std::uint32_t meshId = 0; meshId < scene->mNumMeshes; meshId++) {
            const aiMesh* mesh = scene->mMeshes[meshId];

            for (std::uint32_t faceId = 0; faceId < mesh->mNumFaces; faceId++) {
                const aiFace* face = &mesh->mFaces[faceId];

                for (std::uint32_t indexId = 0; indexId < face->mNumIndices; indexId++) {
                    std::uint32_t index = face->mIndices[indexId];

                    const aiVector3D* vertex = &mesh->mVertices[index];
                    m_geometryPos.insert(m_geometryPos.end(), {vertex->x, vertex->y, vertex->z, 1.0f});

                    const aiVector3D* normal = &mesh->mNormals[index];
                    m_geometryNormal.insert(m_geometryNormal.end(), {normal->x, normal->y, normal->z, 0.0f});
                }
            }
        }
        std::cout << "obj loaded" << std::endl;

        m_bvh.build(m_geometryPos);

        glGenBuffers(1, &m_ssboGeometryPos);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboGeometryPos);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(float) * m_geometryPos.size(), m_geometryPos.data(), GL_DYNAMIC_DRAW);

        glGenBuffers(1, &m_ssboGeometryNormal);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboGeometryNormal);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(float) * m_geometryNormal.size(), m_geometryNormal.data(), GL_STATIC_DRAW);

        glGenBuffers(1, &m_ssboGeometryColor);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboGeometryColor);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(float) * m_geometryColor.size(), m_geometryColor.data(), GL_STATIC_DRAW);

        const auto& leafs = m_bvh.getLeafs();
        glGenBuffers(1, &m_ssboBvhLeaf);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboBvhLeaf);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(std::uint32_t) * leafs.size(), leafs.data(), GL_DYNAMIC_DRAW);

        const auto& aabbMins = m_bvh.getAABBMins();
        glGenBuffers(1, &m_ssboBvhAABBMin);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboBvhAABBMin);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(float) * aabbMins.size(), aabbMins.data(), GL_DYNAMIC_DRAW);

        const auto& aabbMaxs = m_bvh.getAABBMaxs();
        glGenBuffers(1, &m_ssboBvhAABBMax);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboBvhAABBMax);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(float) * aabbMaxs.size(), aabbMaxs.data(), GL_DYNAMIC_DRAW);

        const auto& childs = m_bvh.getChilds();
        glGenBuffers(1, &m_ssboBvhChild);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboBvhChild);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(std::uint32_t) * childs.size(), childs.data(), GL_DYNAMIC_DRAW);

        const auto& primitives = m_bvh.getPrimitives();
        glGenBuffers(1, &m_ssboBvhPrimitive);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboBvhPrimitive);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(std::uint32_t) * primitives.size(), primitives.data(), GL_DYNAMIC_DRAW);
	}

	~Render() {
		glDeleteProgram(m_program);
		glDeleteShader(m_shader);
		glDeleteFramebuffers(1, &m_fbo);
		glDeleteTextures(1, &m_fboTexture);
	}

    void render(float delta) {
        std::cout << 1.0f / delta << std::endl;

        std::uint32_t workgroupSizeX = 8;
        std::uint32_t workgroupSizeY = 8;

        m_timer += delta;

		glUseProgram(m_program);
		glBindImageTexture(0, m_fboTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_ssboGeometryPos);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_ssboGeometryNormal);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_ssboGeometryColor);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_ssboBvhLeaf);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, m_ssboBvhAABBMin);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, m_ssboBvhAABBMax);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, m_ssboBvhChild);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, m_ssboBvhPrimitive);
        glUniform1f(glGetUniformLocation(m_program, "u_timer"), m_timer);
        glUniformMatrix4fv(glGetUniformLocation(m_program, "u_viewInv"), 1, GL_FALSE, &m_viewInv[0][0]);
        glDispatchCompute((m_width + workgroupSizeX - 1) / workgroupSizeX, (m_height + workgroupSizeY - 1) / workgroupSizeY, 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	}
};

class DeltaTime {
private:
    std::chrono::time_point<std::chrono::system_clock> m_last;

public:
    DeltaTime() :
        m_last(std::chrono::system_clock::now())
    { }

    float get() {
        auto now = std::chrono::system_clock::now();
        float delta = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last).count() / 1000.0f;
        m_last = now;
        return delta;
    }
};

int main() {
    const std::uint32_t width = 1600;
    const std::uint32_t height = 900;

	SDL_Init(SDL_INIT_VIDEO);
	SDL_Window* window = SDL_CreateWindow("bvh test", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
	SDL_GLContext glContext = SDL_GL_CreateContext(window);
	Render render(width, height);

    DeltaTime deltaTime;

	bool running = true;
	while (running) {
        float delta = deltaTime.get();
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			switch (e.type) {
			case SDL_QUIT: {
				running = false;
				break;
			}
			}
		}
        render.render(delta);
        SDL_GL_SwapWindow(window);
    }

	SDL_GL_DeleteContext(glContext);
	SDL_DestroyWindow(window);
	SDL_Quit();
}
