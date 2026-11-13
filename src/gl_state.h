#pragma once
#include <GLES3/gl3.h>
struct GlState {
    GLint prog,vao,vbo,ebo,tex;
    GLint bsrcrgb,bdstrgb,bsrca,bdsta,beqrgb,beqa;
    GLboolean blend,depth,cull,scissor;
    GLint scbox[4],vp[4],atex;
    void save() {
        glGetIntegerv(GL_CURRENT_PROGRAM,&prog);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&vbo);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&ebo);
        glGetIntegerv(GL_BLEND_SRC_RGB,&bsrcrgb);
        glGetIntegerv(GL_BLEND_DST_RGB,&bdstrgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA,&bsrca);
        glGetIntegerv(GL_BLEND_DST_ALPHA,&bdsta);
        glGetIntegerv(GL_BLEND_EQUATION_RGB,&beqrgb);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA,&beqa);
        blend=glIsEnabled(GL_BLEND); depth=glIsEnabled(GL_DEPTH_TEST);
        cull=glIsEnabled(GL_CULL_FACE); scissor=glIsEnabled(GL_SCISSOR_TEST);
        glGetIntegerv(GL_SCISSOR_BOX,scbox); glGetIntegerv(GL_VIEWPORT,vp);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&atex);
        glActiveTexture(GL_TEXTURE0); glGetIntegerv(GL_TEXTURE_BINDING_2D,&tex);
    }
    void restore() {
        glUseProgram(prog); glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER,vbo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo);
        glBlendEquationSeparate(beqrgb,beqa);
        glBlendFuncSeparate(bsrcrgb,bdstrgb,bsrca,bdsta);
        blend?glEnable(GL_BLEND):glDisable(GL_BLEND);
        depth?glEnable(GL_DEPTH_TEST):glDisable(GL_DEPTH_TEST);
        cull?glEnable(GL_CULL_FACE):glDisable(GL_CULL_FACE);
        scissor?glEnable(GL_SCISSOR_TEST):glDisable(GL_SCISSOR_TEST);
        glScissor(scbox[0],scbox[1],scbox[2],scbox[3]);
        glViewport(vp[0],vp[1],vp[2],vp[3]);
        glActiveTexture(atex); glBindTexture(GL_TEXTURE_2D,tex);
    }
};
