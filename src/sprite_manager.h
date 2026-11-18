#ifndef SPRITE_MANAGER_H
#define SPRITE_MANAGER_H

#include <array>
#include <cstddef>
#include <limits>
#include <stdexcept>

#include "glad/glad.h"
#include "basic_transform.h"
#include "texture_atlas.h"
#include "vector_util.h"
#include <SFML/Graphics.hpp>
#include <SFML/OpenGL.hpp>

#define GL_SILENCE_DEPRECATION  // silences macOS OpenGL deprecation warnings

#define glCheck(expr)                                                                              \
    expr;                                                                                          \
    if (int e = glGetError()) {                                                                    \
        std::cerr << "OpenGL error: " << e << " in file " __FILE__ << ", line number " << __LINE__ \
                  << '\n';                                                                         \
        abort();                                                                                   \
    }

using SpriteID = size_t;

class SpriteManager {
public:
    SpriteManager(const sf::Vector2u& atlasSize, unsigned padding)
        : mVertices(sf::PrimitiveType::Triangles),
          mNumSprites{0},
          mAtlas(atlasSize, padding),
          mShader(std::filesystem::path{"main.vert"}, std::filesystem::path{"main.frag"}),
          mSFMLShader(std::filesystem::path{"main_sfml.vert"},
                      std::filesystem::path{"main.frag"}) {
        GLuint program = mShader.getNativeHandle();
        mPositionAttribLocation = glGetAttribLocation(program, "position");
        mTexCoordsAttribLocation = glGetAttribLocation(program, "texCoords");
        mSpriteNumberAttribLocation = glGetAttribLocation(program, "spriteNumber");
        mTextureUniformLocation = glGetUniformLocation(program, "texture");
        mTransformsUniformLocation = glGetUniformLocation(program, "transforms");
        mNumTransformsUniformLocation = glGetUniformLocation(program, "numTransforms");
        if (mPositionAttribLocation < 0 || mTexCoordsAttribLocation < 0 ||
            mSpriteNumberAttribLocation < 0 || mTextureUniformLocation < 0 ||
            mTransformsUniformLocation < 0 || mNumTransformsUniformLocation < 0) {
            throw std::runtime_error("Missing sprite shader attribute or uniform");
        }
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &mMaxTransforms);
        glGenBuffers(1, &mVerticesVBO);
        glGenBuffers(1, &mSpriteNumberVBO);
        glGenTextures(1, &mTransformsTexture);
    }

    ~SpriteManager() {
        glDeleteBuffers(1, &mVerticesVBO);
        glDeleteBuffers(1, &mSpriteNumberVBO);
        glDeleteTextures(1, &mTransformsTexture);
    }

    SpriteManager(const SpriteManager&) = delete;
    SpriteManager& operator=(const SpriteManager&) = delete;

    TextureID registerTexture(const std::string& filepath) {
        return mAtlas.registerTexture(filepath);
    };

    SpriteID createSprite(TextureID textureID) {
        if (mNumSprites >= static_cast<size_t>(mMaxTransforms)) {
            throw std::runtime_error("Too many sprites for the 1D transforms texture");
        }
        SpriteID id = mNumSprites;
        mVertices.resize((id + 1) * 6);
        mTransforms.emplace_back();
        mSpriteNumbers.insert(mSpriteNumbers.end(), 6, static_cast<float>(id));
        ++mNumSprites;
        setTexture(id, textureID);
        return id;
    };

    void setTexture(SpriteID sprite, TextureID newTexture) {
        assert(sprite < mNumSprites);
        sf::Vector2f size(mAtlas.getSizePixels(newTexture));
        sf::FloatRect uv = mAtlas.getBoundsUV(newTexture);
        const std::array<sf::Vector2f, 6> corners = {
            sf::Vector2f{0.f, 0.f}, sf::Vector2f{1.f, 0.f}, sf::Vector2f{1.f, 1.f},
            sf::Vector2f{0.f, 0.f}, sf::Vector2f{1.f, 1.f}, sf::Vector2f{0.f, 1.f}};
        for (size_t i = 0; i < corners.size(); ++i) {
            sf::Vertex& vertex = mVertices[sprite * 6 + i];
            vertex.position = corners[i] * size;
            vertex.texCoords = uv.position + corners[i] * uv.size;
        }
    };

    void draw(sf::RenderTarget& target, sf::RenderStates states) const {
        sf::VertexArray vertices = mVertices;
        for (size_t i = 0; i < vertices.getVertexCount(); ++i) {
            vertices[i].position = mTransforms[i / 6].applyPoint(vertices[i].position);
        }
        mSFMLShader.setUniform("texture", sf::Shader::CurrentTexture);
        states.texture = &mAtlas.getTexture();
        states.shader = &mSFMLShader;
        states.coordinateType = sf::CoordinateType::Normalized;
        target.draw(vertices, states);
    };

    void drawGL() {
        if (mNumSprites == 0) {
            return;
        }
        glUseProgram(mShader.getNativeHandle());

        glBindBuffer(GL_ARRAY_BUFFER, mVerticesVBO);
        glBufferData(GL_ARRAY_BUFFER, mVertices.getVertexCount() * sizeof(sf::Vertex),
                     &mVertices[0], GL_STREAM_DRAW);
        glVertexAttribPointer(mPositionAttribLocation, 2, GL_FLOAT, GL_FALSE, sizeof(sf::Vertex),
                              reinterpret_cast<const void*>(offsetof(sf::Vertex, position)));
        glVertexAttribPointer(mTexCoordsAttribLocation, 2, GL_FLOAT, GL_FALSE, sizeof(sf::Vertex),
                              reinterpret_cast<const void*>(offsetof(sf::Vertex, texCoords)));
        glEnableVertexAttribArray(mPositionAttribLocation);
        glEnableVertexAttribArray(mTexCoordsAttribLocation);

        glBindBuffer(GL_ARRAY_BUFFER, mSpriteNumberVBO);
        glBufferData(GL_ARRAY_BUFFER, mSpriteNumbers.size() * sizeof(float),
                     mSpriteNumbers.data(), GL_STREAM_DRAW);
        glVertexAttribPointer(mSpriteNumberAttribLocation, 1, GL_FLOAT, GL_FALSE, sizeof(float),
                              nullptr);
        glEnableVertexAttribArray(mSpriteNumberAttribLocation);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, mAtlas.getTexture().getNativeHandle());
        glUniform1i(mTextureUniformLocation, 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_1D, mTransformsTexture);
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA32F_ARB, static_cast<GLsizei>(mTransforms.size()),
                     0, GL_RGBA, GL_FLOAT, mTransforms.data());
        glUniform1i(mTransformsUniformLocation, 1);
        glUniform1i(mNumTransformsUniformLocation, static_cast<GLint>(mTransforms.size()));

        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mVertices.getVertexCount()));

        glDisableVertexAttribArray(mPositionAttribLocation);
        glDisableVertexAttribArray(mTexCoordsAttribLocation);
        glDisableVertexAttribArray(mSpriteNumberAttribLocation);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindTexture(GL_TEXTURE_1D, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);
    };

    BasicTransform& getTransform(SpriteID id) { return mTransforms.at(id); }

    const TextureAtlas& getAtlas() const { return mAtlas; };

private:
    sf::VertexArray mVertices;
    std::vector<BasicTransform> mTransforms;
    size_t mNumSprites;
    TextureAtlas mAtlas;
    mutable sf::Shader mShader;
    mutable sf::Shader mSFMLShader;
    GLint mMaxTransforms;

    // Use the below only in Batched rendering (part 2):
    std::vector<float> mSpriteNumbers;

    GLint mTextureUniformLocation;
    GLint mTransformsUniformLocation;
    GLint mNumTransformsUniformLocation;

    GLint mPositionAttribLocation;
    GLint mTexCoordsAttribLocation;
    GLint mSpriteNumberAttribLocation;

    GLuint mVerticesVBO;
    GLuint mSpriteNumberVBO;
    GLuint mTransformsTexture;
};

#endif  // SPRITE_MANAGER_H
