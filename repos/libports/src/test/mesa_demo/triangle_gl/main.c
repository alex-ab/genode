#define GL_GLEXT_PROTOTYPES
#include <stdlib.h>
#ifdef __APPLE__
#  include <GLUT/glut.h>
#else
#include <GL/gl.h>
#include <GL/glext.h>
#include <EGL/egl.h>
#include "eglut.h"
#endif
#include <math.h>
#include <stdio.h>


/*
 * GLUT callbacks:
 */
static void update_timer(void)
{
	eglutPostRedisplay();
}

static void render(void)
{
	static float r_color = 0.0f;
	static float rotate  = 0.0f;

	glClearColor(r_color, 0.f, 0.f, 0.f);
	glClear(GL_COLOR_BUFFER_BIT);

	glLoadIdentity();

	glRotatef(rotate, 0.f, 0.f, 1.f);
	glBegin(GL_TRIANGLES);
		glVertex3f( 0.0f, 1.0f, 0.0f);
		glVertex3f(-1.0f,-1.0f, 0.0f);
		glVertex3f( 1.0f,-1.0f, 0.0f);
	glEnd();

	glFlush();

	printf("color=%f rotate=%f\n", r_color, rotate);

	rotate += 90.f;
	if (rotate >= 360.f)
		rotate  = 0.0f;

	if (r_color >= 1.0f) {
		r_color  = 0.0f;
	} else {
		r_color += 0.5f;
	}
}

/*
 * Entry point
 */
int eglut_main(int argc, char** argv)
{
	eglutInit(argc, argv);
	eglutInitWindowSize(600, 600);
	eglutInitAPIMask(EGLUT_OPENGL_BIT);
	eglutCreateWindow("Triangle");
	eglutIdleFunc(&update_timer);
	eglutDisplayFunc(&render);

	eglutMainLoop();
	return 0;
}
