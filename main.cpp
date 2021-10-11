#include "AccelerationStructures.hpp"

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

class Render {
private:
    class ComputeShader {
    private:
        GLuint m_shader;
        GLuint m_program;

    public:
        ComputeShader(const std::string& name) {
            int errLen = 0;
            char buffer[1024] = {};
            const char* sourcePtr = nullptr;
            std::string shaderSource;
            std::ifstream input(name);
            input.seekg(0, std::ios::end);
            std::uint32_t size = input.tellg();
            input.seekg(0, std::ios::beg);
            shaderSource.resize(size, 0);
            input.read(&shaderSource[0], size);

            sourcePtr = shaderSource.c_str();
            m_shader = glCreateShader(GL_COMPUTE_SHADER);
            glShaderSource(m_shader, 1, &sourcePtr, nullptr);
            glCompileShader(m_shader);
            glGetShaderInfoLog(m_shader, sizeof(buffer), &errLen, buffer);
            if (errLen > 0) std::cout << name << ": " << std::string(buffer, buffer + errLen) << std::endl;

            m_program = glCreateProgram();
            glAttachShader(m_program, m_shader);
            glLinkProgram(m_program);
            glGetProgramInfoLog(m_program, sizeof(buffer), &errLen, buffer);
            if (errLen > 0) std::cout << name << ": " << std::string(buffer, buffer + errLen) << std::endl;
        }

        ~ComputeShader() {
            glDeleteProgram(m_program);
            glDeleteShader(m_shader);
        }

        GLuint getProgram() {
            return m_program;
        }
    };

	std::uint32_t m_width;
	std::uint32_t m_height;

    float m_timer;

	GLuint m_fbo;
	GLuint m_fboTexture;

    std::optional<ComputeShader> m_programGenerate;
    std::optional<ComputeShader> m_programExtend;
    std::optional<ComputeShader> m_programShade;

    Assimp::Importer m_meshImporter;

    GLuint m_ssboTlasGetAABB;
    GLuint m_ssboTlasGetGeometry;
    GLuint m_ssboTlasGetChild;
    GLuint m_ssboTlasGetPrimitiveId;
    GLuint m_ssboTlasIsLeaf;
    GLuint m_ssboTlasGetBlasNodeOffset;
    GLuint m_ssboTlasGetBlasGeometryOffset;

    GLuint m_ssboBlasGetAABB;
    GLuint m_ssboBlasGetGeometry;
    GLuint m_ssboBlasGetChild;
    GLuint m_ssboBlasGetPrimitiveId;
    GLuint m_ssboBlasIsLeaf;

    GLuint m_ssboCounter;
    GLuint m_ssboRayBufferRead;
    GLuint m_ssboRayBufferWrite;
    GLuint m_ssboIntersectionBuffer;

    AccelerationStructures m_accels;
    glm::mat4 m_viewInv;

public:
	Render(std::uint32_t width, std::uint32_t height) :
		m_width(width),
        m_height(height),
        m_timer(0.0f),
        m_viewInv(glm::transpose(glm::inverse(glm::lookAt(glm::vec3(0.0f, 10.0f, 50.0f), glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)))))
    {
        glewInit();

        m_programGenerate.emplace("generate.glsl");
        m_programExtend.emplace("extend.glsl");
        m_programShade.emplace("shade.glsl");

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

        float totalBlasBuildTime = 0.0;

        const aiScene* scene = m_meshImporter.ReadFile("sponza.obj", aiProcess_Triangulate);
        for (std::uint32_t meshId = 0; meshId < scene->mNumMeshes; meshId++) {
            const aiMesh* mesh = scene->mMeshes[meshId];

            bool emissive = (meshId == scene->mNumMeshes - 1);

            std::vector<float> triangles;
            for (std::uint32_t faceId = 0; faceId < mesh->mNumFaces; faceId++) {
                const aiFace* face = &mesh->mFaces[faceId];

                for (std::uint32_t indexId = 0; indexId < face->mNumIndices; indexId++) {
                    std::uint32_t index = face->mIndices[indexId];

                    const aiVector3D* vertex = &mesh->mVertices[index];
                    triangles.insert(triangles.end(), {vertex->x, vertex->y, vertex->z, 1.0f});

                    const aiVector3D* normal = &mesh->mNormals[index];
                    triangles.insert(triangles.end(), {normal->x, normal->y, normal->z, 0.0f});

                    triangles.insert(triangles.end(), {emissive ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f});
                }
            }
            DeltaTime deltaTime;
            m_accels.addBLAS(triangles);
            totalBlasBuildTime += deltaTime.get();
        }
        std::cout << "BLAS built in " << totalBlasBuildTime << " seconds" << std::endl;

        DeltaTime deltaTime;
        m_accels.buildTLAS();
        std::cout << "TLAS built in " << deltaTime.get() << " seconds" << std::endl;

        const auto& tlas = m_accels.getTLAS();

        glGenBuffers(1, &m_ssboTlasGetAABB);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboTlasGetAABB);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(float) * tlas.aabbs.size(), tlas.aabbs.data(), GL_DYNAMIC_DRAW);

        glGenBuffers(1, &m_ssboTlasGetGeometry);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboTlasGetGeometry);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(float) * tlas.geometry.size(), tlas.geometry.data(), GL_DYNAMIC_DRAW);

        glGenBuffers(1, &m_ssboTlasGetChild);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboTlasGetChild);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(std::uint32_t) * tlas.children.size(), tlas.children.data(), GL_DYNAMIC_DRAW);

        glGenBuffers(1, &m_ssboTlasGetPrimitiveId);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboTlasGetPrimitiveId);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(std::uint32_t) * tlas.primitives.size(), tlas.primitives.data(), GL_DYNAMIC_DRAW);

        glGenBuffers(1, &m_ssboTlasIsLeaf);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboTlasIsLeaf);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(std::uint32_t) * tlas.leafs.size(), tlas.leafs.data(), GL_DYNAMIC_DRAW);

        glGenBuffers(1, &m_ssboTlasGetBlasNodeOffset);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboTlasGetBlasNodeOffset);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(std::uint32_t) * m_accels.getTlasBlasNodeOffsets().size(), m_accels.getTlasBlasNodeOffsets().data(), GL_DYNAMIC_DRAW);

        glGenBuffers(1, &m_ssboTlasGetBlasGeometryOffset);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboTlasGetBlasGeometryOffset);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(std::uint32_t) * m_accels.getTlasBlasGeometryOffsets().size(), m_accels.getTlasBlasGeometryOffsets().data(), GL_DYNAMIC_DRAW);

        std::vector<float>         blasAABBs;
        std::vector<float>         blasGeometry;
        std::vector<std::uint32_t> blasChildren;
        std::vector<std::uint32_t> blasPrimitives;
        std::vector<std::uint32_t> blasLeafs;

        for (const auto& blasBVH : m_accels.getBLAS()) {
            blasAABBs.insert(blasAABBs.end(), blasBVH.aabbs.begin(), blasBVH.aabbs.end());
            blasGeometry.insert(blasGeometry.end(), blasBVH.geometry.begin(), blasBVH.geometry.end());
            blasChildren.insert(blasChildren.end(), blasBVH.children.begin(), blasBVH.children.end());
            blasPrimitives.insert(blasPrimitives.end(), blasBVH.primitives.begin(), blasBVH.primitives.end());
            blasLeafs.insert(blasLeafs.end(), blasBVH.leafs.begin(), blasBVH.leafs.end());
        }

        glGenBuffers(1, &m_ssboBlasGetAABB);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboBlasGetAABB);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(float) * blasAABBs.size(), blasAABBs.data(), GL_STATIC_DRAW);

        glGenBuffers(1, &m_ssboBlasGetGeometry);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboBlasGetGeometry);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(float) * blasGeometry.size(), blasGeometry.data(), GL_STATIC_DRAW);

        glGenBuffers(1, &m_ssboBlasGetChild);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboBlasGetChild);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(std::uint32_t) * blasChildren.size(), blasChildren.data(), GL_STATIC_DRAW);

        glGenBuffers(1, &m_ssboBlasGetPrimitiveId);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboBlasGetPrimitiveId);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(std::uint32_t) * blasPrimitives.size(), blasPrimitives.data(), GL_STATIC_DRAW);

        glGenBuffers(1, &m_ssboBlasIsLeaf);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboBlasIsLeaf);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(std::uint32_t) * blasLeafs.size(), blasLeafs.data(), GL_STATIC_DRAW);

        glGenBuffers(1, &m_ssboCounter);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboCounter);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(std::uint32_t), nullptr, GL_DYNAMIC_DRAW);

        glGenBuffers(1, &m_ssboRayBufferRead);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboRayBufferRead);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(float) * 4 * 2 * m_width * m_height, nullptr, GL_DYNAMIC_DRAW);

        glGenBuffers(1, &m_ssboRayBufferWrite);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboRayBufferWrite);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(float) * 4 * 2 * m_width * m_height, nullptr, GL_DYNAMIC_DRAW);

        glGenBuffers(1, &m_ssboIntersectionBuffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboIntersectionBuffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(float) * 4 * m_width * m_height, nullptr, GL_DYNAMIC_DRAW);
    }

	~Render() {
		glDeleteFramebuffers(1, &m_fbo);
		glDeleteTextures(1, &m_fboTexture);
	}

    std::uint32_t generate() {
        std::uint32_t workgroupSizeX = 8;
        std::uint32_t workgroupSizeY = 8;

        std::uint32_t counter = 0;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboCounter);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(std::uint32_t), &counter);

        glUseProgram(m_programGenerate->getProgram());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_ssboCounter);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_ssboRayBufferWrite);
        glUniform2i(glGetUniformLocation(m_programGenerate->getProgram(), "u_screenSize"), m_width, m_height);
        glUniformMatrix4fv(glGetUniformLocation(m_programGenerate->getProgram(), "u_viewInv"), 1, GL_TRUE, &m_viewInv[0][0]);
        glDispatchCompute((m_width + workgroupSizeX - 1) / workgroupSizeX, (m_height + workgroupSizeY - 1) / workgroupSizeY, 1);
        glMemoryBarrier(GL_ALL_BARRIER_BITS);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        glFinish();

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboCounter);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(std::uint32_t), &counter);

        return counter;
    }

    void extend(std::uint32_t rayBufferSize) {
        std::uint32_t workgroupSizeX = 64;

        std::swap(m_ssboRayBufferRead, m_ssboRayBufferWrite);

        glUseProgram(m_programExtend->getProgram());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 13, m_ssboIntersectionBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 14, m_ssboRayBufferRead);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_ssboTlasGetAABB);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_ssboTlasGetGeometry);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_ssboTlasGetChild);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_ssboTlasGetPrimitiveId);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, m_ssboTlasIsLeaf);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, m_ssboTlasGetBlasNodeOffset);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, m_ssboTlasGetBlasGeometryOffset);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, m_ssboBlasGetAABB);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9, m_ssboBlasGetGeometry);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, m_ssboBlasGetChild);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 11, m_ssboBlasGetPrimitiveId);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 12, m_ssboBlasIsLeaf);
        glUniform1ui(glGetUniformLocation(m_programExtend->getProgram(), "u_raysCount"), rayBufferSize);
        glDispatchCompute((rayBufferSize + workgroupSizeX - 1) / workgroupSizeX, 1, 1);
        glMemoryBarrier(GL_ALL_BARRIER_BITS);
        glFinish();
    }

    std::uint32_t shade(std::uint32_t rayBufferSize, std::uint32_t iteration) {
        std::uint32_t workgroupSizeX = 64;

        std::uint32_t counter = 0;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboCounter);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(std::uint32_t), &counter);

        glUseProgram(m_programShade->getProgram());
        glBindImageTexture(0, m_fboTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 16, m_ssboRayBufferWrite);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 15, m_ssboCounter);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 13, m_ssboIntersectionBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 14, m_ssboRayBufferRead);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_ssboTlasGetAABB);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_ssboTlasGetGeometry);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_ssboTlasGetChild);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_ssboTlasGetPrimitiveId);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, m_ssboTlasIsLeaf);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, m_ssboTlasGetBlasNodeOffset);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, m_ssboTlasGetBlasGeometryOffset);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, m_ssboBlasGetAABB);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9, m_ssboBlasGetGeometry);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 10, m_ssboBlasGetChild);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 11, m_ssboBlasGetPrimitiveId);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 12, m_ssboBlasIsLeaf);
        glUniform1ui(glGetUniformLocation(m_programShade->getProgram(), "u_iteration"), iteration);
        glUniform1ui(glGetUniformLocation(m_programShade->getProgram(), "u_raysCount"), rayBufferSize);
        glUniform1f(glGetUniformLocation(m_programShade->getProgram(), "u_timer"), m_timer);
        glDispatchCompute((rayBufferSize + workgroupSizeX - 1) / workgroupSizeX, 1, 1);
        glMemoryBarrier(GL_ALL_BARRIER_BITS);
        glFinish();

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboCounter);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(std::uint32_t), &counter);

        return counter;
    }

    void connect() {

    }

    void render(float delta) {
        //std::cout << 1.0 / delta << std::endl;

        m_timer += delta;

        std::uint32_t rays = generate();

        for (std::uint32_t i = 0; i < 2; i++) {
            extend(rays);
            rays = shade(rays, i);
        }

        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

        std::cout << delta << "; " << rays << std::endl;
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
    float delta = 0.0f;

	bool running = true;
	while (running) {
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

        delta = deltaTime.get();
    }

	SDL_GL_DeleteContext(glContext);
	SDL_DestroyWindow(window);
	SDL_Quit();
}
