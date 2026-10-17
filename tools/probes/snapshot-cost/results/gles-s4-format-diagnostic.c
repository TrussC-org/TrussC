#define _GNU_SOURCE
#include <GLES3/gl3.h>
#include <dlfcn.h>
#include <stdio.h>
void glBlitFramebuffer(GLint a,GLint b,GLint c,GLint d,GLint e,GLint f,GLint g,GLint h,GLbitfield mask,GLenum filter){
 static void(*real)(GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum);
 if(!real)real=dlsym(RTLD_NEXT,"glBlitFramebuffer");
 GLint src[4]={},dst[4]={},rb=0,db=0,samples=0;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&rb);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&db);glGetIntegerv(GL_SAMPLES,&samples);
 for(int i=0;i<4;++i){glGetFramebufferAttachmentParameteriv(GL_READ_FRAMEBUFFER,rb?GL_COLOR_ATTACHMENT0:GL_BACK,GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE+i,&src[i]);glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER,db?GL_COLOR_ATTACHMENT0:GL_BACK,GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE+i,&dst[i]);}
 GLenum before=glGetError();real(a,b,c,d,e,f,g,h,mask,filter);GLenum after=glGetError();
 fprintf(stderr,"BLIT DIAGNOSTIC src=%d/rgbA=%d,%d,%d,%d dst=%d/rgbA=%d,%d,%d,%d drawSamples=%d errors=0x%x->0x%x\n",rb,src[0],src[1],src[2],src[3],db,dst[0],dst[1],dst[2],dst[3],samples,before,after);
}
