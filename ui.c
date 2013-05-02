/* balance.lv2 GUI
 *
 * Copyright (C) 2013 Robin Gareus <robin@gareus.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#define _GNU_SOURCE
#define GL_GLEXT_PROTOTYPES

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <pthread.h>

#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>

#include "lv2/lv2plug.in/ns/extensions/ui/ui.h"
#include "pugl/pugl.h"

#ifdef __APPLE__
#include "OpenGL/glu.h"
#else
#include <GL/glu.h>
#endif

#include <FTGL/ftgl.h>

#ifndef FONTFILE
#define FONTFILE "/usr/share/fonts/truetype/ttf-bitstream-vera/VeraBd.ttf"
#endif

#ifndef FONTSIZE
#define FONTSIZE 36
#endif


#include "uris.h"
#include "ui_model.h"

/* ui-model scale -- on screen we use [-1..+1] orthogonal projection */
#define SCALE (0.2f)

#define SIGNUM(a) (a < 0 ? -1 : 1)
#define CTRLWIDTH2(ctrl) (SCALE * (ctrl).w / 2.0)
#define CTRLHEIGHT2(ctrl) (SCALE * (ctrl).h / 2.0)

#define MOUSEOVER(ctrl, mousex, mousey) \
  (   (mousex) >= (ctrl).x * SCALE - CTRLWIDTH2(ctrl) \
   && (mousex) <= (ctrl).x * SCALE + CTRLWIDTH2(ctrl) \
   && (mousey) >= (ctrl).y * SCALE - CTRLHEIGHT2(ctrl) \
   && (mousey) <= (ctrl).y * SCALE + CTRLHEIGHT2(ctrl) )


static inline int MOUSEIN(
    const float X0, const float X1,
    const float Y0, const float Y1,
    const float mousex, const float mousey) {
  return (
      (mousex) >= (X0)
   && (mousex) <= (X1)
   && (mousey) >= (Y0)
   && (mousey) <= (Y1)
   );
}

/* total number of interactive objects */
#define TOTAL_OBJ (10)

typedef struct {
  int type; // type ID from ui_model.h
  float min, max, cur, dfl;  // value range and current value
  float x,y; // matrix position
  float w,h; // bounding box
  int texID; // texture ID
  int ctrlidx;
} blcwidget;

typedef struct {
  LV2UI_Write_Function write;
  LV2UI_Controller     controller;

  LV2_URID_Map* map;
  balanceURIs  uris;

  PuglView*            view;
  int                  width;
  int                  height;
  int                  initialized;

#ifdef OLD_SUIL
  pthread_t            thread;
  int                  exit;
#endif

  /* OpenGL */
  GLuint * vbo;
  GLuint * vinx;
  GLuint texID[4]; // textures
  GLdouble matrix[16]; // used for mouse mapping

  double rot[3], off[3], scale; // global projection

  /* interactive control objexts */
  blcwidget ctrls[TOTAL_OBJ];

  /* mouse drag status */
  int dndid;
  float dndscale;
  float dndval;
  float dndx, dndy;
  int hoverid;

  float p_bal[2];
  float p_dly[2];
  float p_mtr_in[2];
  float p_mtr_out[2];

  FTGLfont *font_small;
} BLCui;


/******************************************************************************
 * 3D projection
 */

/* invert projection matrix -- code from GLU */
static bool invertMatrix(const double m[16], double invOut[16]) {
  double inv[16], det;
  int i;

  inv[0] = m[5]  * m[10] * m[15] -
	   m[5]  * m[11] * m[14] -
	   m[9]  * m[6]  * m[15] +
	   m[9]  * m[7]  * m[14] +
	   m[13] * m[6]  * m[11] -
	   m[13] * m[7]  * m[10];

  inv[4] = -m[4]  * m[10] * m[15] +
	    m[4]  * m[11] * m[14] +
	    m[8]  * m[6]  * m[15] -
	    m[8]  * m[7]  * m[14] -
	    m[12] * m[6]  * m[11] +
	    m[12] * m[7]  * m[10];

  inv[8] = m[4]  * m[9]  * m[15] -
	   m[4]  * m[11] * m[13] -
	   m[8]  * m[5]  * m[15] +
	   m[8]  * m[7]  * m[13] +
	   m[12] * m[5]  * m[11] -
	   m[12] * m[7]  * m[9];

  inv[12] = -m[4]  * m[9]  * m[14] +
	     m[4]  * m[10] * m[13] +
	     m[8]  * m[5]  * m[14] -
	     m[8]  * m[6]  * m[13] -
	     m[12] * m[5]  * m[10] +
	     m[12] * m[6]  * m[9];

  inv[1] = -m[1]  * m[10] * m[15] +
	    m[1]  * m[11] * m[14] +
	    m[9]  * m[2]  * m[15] -
	    m[9]  * m[3]  * m[14] -
	    m[13] * m[2]  * m[11] +
	    m[13] * m[3]  * m[10];

  inv[5] = m[0]  * m[10] * m[15] -
	   m[0]  * m[11] * m[14] -
	   m[8]  * m[2]  * m[15] +
	   m[8]  * m[3]  * m[14] +
	   m[12] * m[2]  * m[11] -
	   m[12] * m[3]  * m[10];

  inv[9] = -m[0]  * m[9]  * m[15] +
	    m[0]  * m[11] * m[13] +
	    m[8]  * m[1]  * m[15] -
	    m[8]  * m[3]  * m[13] -
	    m[12] * m[1]  * m[11] +
	    m[12] * m[3]  * m[9];

  inv[13] = m[0]  * m[9]  * m[14] -
	    m[0]  * m[10] * m[13] -
	    m[8]  * m[1]  * m[14] +
	    m[8]  * m[2]  * m[13] +
	    m[12] * m[1]  * m[10] -
	    m[12] * m[2]  * m[9];

  inv[2] = m[1]  * m[6] * m[15] -
	   m[1]  * m[7] * m[14] -
	   m[5]  * m[2] * m[15] +
	   m[5]  * m[3] * m[14] +
	   m[13] * m[2] * m[7] -
	   m[13] * m[3] * m[6];

  inv[6] = -m[0]  * m[6] * m[15] +
	    m[0]  * m[7] * m[14] +
	    m[4]  * m[2] * m[15] -
	    m[4]  * m[3] * m[14] -
	    m[12] * m[2] * m[7] +
	    m[12] * m[3] * m[6];

  inv[10] = m[0]  * m[5] * m[15] -
	    m[0]  * m[7] * m[13] -
	    m[4]  * m[1] * m[15] +
	    m[4]  * m[3] * m[13] +
	    m[12] * m[1] * m[7] -
	    m[12] * m[3] * m[5];

  inv[14] = -m[0]  * m[5] * m[14] +
	     m[0]  * m[6] * m[13] +
	     m[4]  * m[1] * m[14] -
	     m[4]  * m[2] * m[13] -
	     m[12] * m[1] * m[6] +
	     m[12] * m[2] * m[5];

  inv[3] = -m[1] * m[6] * m[11] +
	    m[1] * m[7] * m[10] +
	    m[5] * m[2] * m[11] -
	    m[5] * m[3] * m[10] -
	    m[9] * m[2] * m[7] +
	    m[9] * m[3] * m[6];

  inv[7] = m[0] * m[6] * m[11] -
	   m[0] * m[7] * m[10] -
	   m[4] * m[2] * m[11] +
	   m[4] * m[3] * m[10] +
	   m[8] * m[2] * m[7] -
	   m[8] * m[3] * m[6];

  inv[11] = -m[0] * m[5] * m[11] +
	     m[0] * m[7] * m[9] +
	     m[4] * m[1] * m[11] -
	     m[4] * m[3] * m[9] -
	     m[8] * m[1] * m[7] +
	     m[8] * m[3] * m[5];

  inv[15] = m[0] * m[5] * m[10] -
	    m[0] * m[6] * m[9] -
	    m[4] * m[1] * m[10] +
	    m[4] * m[2] * m[9] +
	    m[8] * m[1] * m[6] -
	    m[8] * m[2] * m[5];

  det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];

  if (det == 0) return false;

  det = 1.0 / det;

  for (i = 0; i < 16; i++)
    invOut[i] = inv[i] * det;

  return true;
}

#ifdef DEBUG_ROTATION_MATRIX
static void print4x4(GLdouble *m) {
  fprintf(stderr,
      "%+0.3lf %+0.3lf %+0.3lf %+0.3lf\n"
      "%+0.3lf %+0.3lf %+0.3lf %+0.3lf\n"
      "%+0.3lf %+0.3lf %+0.3lf %+0.3lf\n"
      "%+0.3lf %+0.3lf %+0.3lf %+0.3lf;\n\n"
      , m[0] , m[1] , m[2] , m[3]
      , m[4] , m[5] , m[6] , m[7]
      , m[8] , m[9] , m[10] , m[11]
      , m[12] , m[13] , m[14] , m[15]
      );
}
#endif

/* apply reverse projection to mouse-pointer, project Z-axis to screen. */
static void project_mouse(PuglView* view, int mx, int my, float *x, float *y) {
  BLCui* ui = (BLCui*)puglGetHandle(view);
  const double fx =  2.0 * (float)mx / ui->width  - 1.0;
  const double fy = -2.0 * (float)my / ui->height + 1.0;
  const double fz = -(fx * ui->matrix[2] + fy * ui->matrix[6]) / ui->matrix[10];

  *x = fx * ui->matrix[0] + fy * ui->matrix[4] + fz * ui->matrix[8] + ui->matrix[12];
  *y = fx * ui->matrix[1] + fy * ui->matrix[5] + fz * ui->matrix[9] + ui->matrix[13];
}


/******************************************************************************
 * Value mapping
 */

static void rmap_val(PuglView* view, const int elem, const float val) {
  BLCui* ui = (BLCui*)puglGetHandle(view);
  if (elem > 4 && elem < 10) {
    int pb = val + 5;
    for (int i=5; i < 10; ++i) {
      if (i==pb) {
	ui->ctrls[i].cur = ui->ctrls[i].max;
      } else {
	ui->ctrls[i].cur = ui->ctrls[i].min;
      }
    }
    return;
  }

  if (ui->ctrls[elem].max == 0) {
    ui->ctrls[elem].cur = ui->ctrls[elem].min + rint(val);
  } else {
    ui->ctrls[elem].cur = val;
  }
}

/* called from lv2 plugin if value has changed (via port_event) */
static float vmap_val(PuglView* view, const int elem) {
  BLCui* ui = (BLCui*)puglGetHandle(view);
  if (ui->ctrls[elem].max == 0) {
    return rint(ui->ctrls[elem].cur - ui->ctrls[elem].min);
  } else {
    return ui->ctrls[elem].cur;
  }
}

/* notify lv2 plugin about changed value */
static void notifyPlugin(PuglView* view, int elem) {
  BLCui* ui = (BLCui*)puglGetHandle(view);
  if (elem > 4 && elem < 10) {
    /* push/radio button special */
    for (int i=5; i < 10; ++i) {
      if (i==elem)
	ui->ctrls[i].cur = ui->ctrls[i].max;
      else
	ui->ctrls[i].cur = ui->ctrls[i].min;
    }
    const float val = elem - 5;
    ui->write(ui->controller, 5, sizeof(float), 0, (const void*)&val);
    return;
  } else {
    const float val = vmap_val(view, elem);
    ui->write(ui->controller, elem, sizeof(float), 0, (const void*)&val);
  }
}

/* process mouse motion, update value */
static void processMotion(PuglView* view, int elem, float dx, float dy) {
  BLCui* ui = (BLCui*)puglGetHandle(view);
  if (elem < 0 || elem >= TOTAL_OBJ) return;
  //const float dist = (fabs(dy) > fabs(dx) ? dy: dx);
  const float dist = dy * ui->dndscale;
  const float oldval = vmap_val(view, elem);

  switch (ui->ctrls[elem].type) {
    case OBJ_DIAL:
      ui->ctrls[elem].cur = ui->dndval + dist * (ui->ctrls[elem].max - ui->ctrls[elem].min);
      if (ui->ctrls[elem].max == 0) {
	if (ui->ctrls[elem].cur > ui->ctrls[elem].max || ui->ctrls[elem].cur < ui->ctrls[elem].min) {
	  const float r = (ui->ctrls[elem].max - ui->ctrls[elem].min);
	  ui->ctrls[elem].cur -= ceil(ui->ctrls[elem].cur / r) * r;
	}
      } else {
	if (ui->ctrls[elem].cur > ui->ctrls[elem].max) ui->ctrls[elem].cur = ui->ctrls[elem].max;
	if (ui->ctrls[elem].cur < ui->ctrls[elem].min) ui->ctrls[elem].cur = ui->ctrls[elem].min;
      }
      break;
    default:
      break;
  }

  if (vmap_val(view, elem) != oldval) {
    puglPostRedisplay(view);
    notifyPlugin(view, elem);
  }
}

/******************************************************************************
 * 3D model loading
 * see http://ksolek.fm.interia.pl/Blender/
 */

static void initMesh(PuglView* view) {
  BLCui* ui = (BLCui*)puglGetHandle(view);
  int i;

  glGenBuffers(OBJECTS_COUNT, ui->vbo);

  for (i = 0; i < OBJECTS_COUNT; i++) {
    glBindBuffer(GL_ARRAY_BUFFER, ui->vbo[i]);
    glBufferData(GL_ARRAY_BUFFER, sizeof (struct vertex_struct) * vertex_count[i], &vertices[vertex_offset_table[i]], GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    transformations[i][10] *= -1.0;
  }

  glGenBuffers(OBJECTS_COUNT, ui->vinx);
  for (i = 0; i < OBJECTS_COUNT; i++) {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ui->vinx[i]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof (indexes[0]) * faces_count[i] * 3, &indexes[indices_offset_table[i]], GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  }
}

#define BUFFER_OFFSET(x)((char *)NULL+(x))

static void drawMesh(PuglView* view, unsigned int index) {
  BLCui* ui = (BLCui*)puglGetHandle(view);

  glPushMatrix();
  glMultMatrixf(transformations[index]);

  glBindBuffer(GL_ARRAY_BUFFER, ui->vbo[index]);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ui->vinx[index]);

  glEnableClientState(GL_VERTEX_ARRAY);
  glVertexPointer(3, GL_FLOAT, sizeof (struct vertex_struct), BUFFER_OFFSET(0));

  glEnableClientState(GL_NORMAL_ARRAY);
  glNormalPointer(GL_FLOAT, sizeof (struct vertex_struct), BUFFER_OFFSET(3 * sizeof (float)));

  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
  glTexCoordPointer(2, GL_FLOAT, sizeof (struct vertex_struct), BUFFER_OFFSET(6 * sizeof (float)));

  glDrawElements(GL_TRIANGLES, faces_count[index] * 3, INX_TYPE, BUFFER_OFFSET(0));

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

  glDisableClientState(GL_TEXTURE_COORD_ARRAY);
  glDisableClientState(GL_NORMAL_ARRAY);
  glDisableClientState(GL_VERTEX_ARRAY);

  glPopMatrix();
}

/******************************************************************************
 * OpenGL textures
 */

#include "textures/background.c"
#include "textures/dial.c"

#define CIMAGE(ID, VARNAME) \
  glGenTextures(1, &ui->texID[ID]); \
  glBindTexture(GL_TEXTURE_2D, ui->texID[ID]); \
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER); \
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER); \
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER); \
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); \
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); \
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, VARNAME.width, VARNAME.height, 0, \
      (VARNAME.bytes_per_pixel == 3 ? GL_RGB : GL_RGBA), \
      GL_UNSIGNED_BYTE, VARNAME.pixel_data); \
  if (atihack) { \
    glEnable(GL_TEXTURE_2D); \
    glGenerateMipmapEXT(GL_TEXTURE_2D); \
    glDisable(GL_TEXTURE_2D); \
  } else { \
    glGenerateMipmapEXT(GL_TEXTURE_2D); \
  }


static void initTextures(PuglView* view) {
  BLCui* ui = (BLCui*)puglGetHandle(view);
  const int atihack = 1; // TODO detect card

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  CIMAGE(0, background_image);
  CIMAGE(1, dial_image);
}


/******************************************************************************
 * OpenGL settings
 */

static void setupOpenGL() {
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glFrontFace(GL_CCW);
  glEnable(GL_CULL_FACE);
  glEnable(GL_DITHER);
  glEnable(GL_MULTISAMPLE);
  glEnable(GL_NORMALIZE);
  glEnable(GL_POLYGON_SMOOTH);
  glEnable (GL_LINE_SMOOTH);
  glShadeModel(GL_SMOOTH);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_SRC_ALPHA_SATURATE);

  glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
  glHint(GL_POLYGON_SMOOTH_HINT, GL_NICEST);
  glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
  glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
  glHint(GL_GENERATE_MIPMAP_HINT, GL_NICEST);
  glHint(GL_FOG_HINT, GL_NICEST);

  //glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); // test & debug
}

static void setupLight() {
  const GLfloat light0_ambient[]  = { 0.2, 0.15, 0.1, 1.0 };
  const GLfloat light0_diffuse[]  = { 1.0, 1.0, 1.0, 1.0 };
  const GLfloat light0_specular[] = { 0.4, 0.4, 0.9, 1.0 };
  const GLfloat light0_position[] = {  1.0, -2.5, -10.0, 0 };
  const GLfloat spot_direction[]  = { -1.0,  2.5,  10.0 };

  glLightfv(GL_LIGHT0, GL_AMBIENT, light0_ambient);
  glLightfv(GL_LIGHT0, GL_DIFFUSE, light0_diffuse);
  glLightfv(GL_LIGHT0, GL_SPECULAR, light0_specular);
  glLightfv(GL_LIGHT0, GL_POSITION, light0_position);
  glLightf(GL_LIGHT0,  GL_SPOT_CUTOFF, 10.0f);
  glLightfv(GL_LIGHT0, GL_SPOT_DIRECTION, spot_direction);
#if 0
  glLightf(GL_LIGHT0,  GL_SPOT_EXPONENT, 120.0);
  glLightf(GL_LIGHT0,  GL_CONSTANT_ATTENUATION, 1.5);
  glLightf(GL_LIGHT0,  GL_LINEAR_ATTENUATION, 0.5);
  glLightf(GL_LIGHT0,  GL_QUADRATIC_ATTENUATION, 0.2);

  const GLfloat global_ambient[]  = { 0.2, 0.2, 0.2, 1.0 };
  glLightModelfv(GL_LIGHT_MODEL_AMBIENT, global_ambient);
  glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
  glEnable(GL_COLOR_MATERIAL);
#endif

  glEnable(GL_LIGHTING);
  glEnable(GL_LIGHT0);
}

/******************************************************************************
 * drawing help functions
 */

static void
render_text(PuglView* view, const char *text, float x, float y, float z, int align)
{
  BLCui* ui = (BLCui*)puglGetHandle(view);
  const GLfloat mat_b[] = {0.0, 0.0, 0.0, 1.0};
  const GLfloat mat_r[] = {0.1, 0.95, 0.15, 1.0};
  float bb[6];

  glPushMatrix();
  glLoadIdentity();

  glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mat_b);
  glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, mat_b);
  glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, mat_r);

  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
  glScalef(0.002,0.002,1.00);
  ftglGetFontBBox(ui->font_small, text, -1, bb);
#if 0
  printf("%.2f %.2f %.2f  %.2f %.2f %.2f\n",
      bb[0], bb[1], bb[2], bb[3], bb[4], bb[5]);
#endif
  switch(align) {
    case 4: // center + bottom
      glTranslatef(
	  (bb[3] - bb[0])/-2.0,
	  0,
	  0);
      break;
    case 1: // center + middle
      glTranslatef(
	  (bb[3] - bb[0])/-2.0,
	  (bb[4] - bb[1])/-2.0,
	  0);
      break;
    case 2: // right
      glTranslatef(
	  (bb[3] - bb[0])/-1.0,
	  (bb[4] - bb[1])/-1.0,
	  0);
      break;
    case 3: // left bottom
      break;
    default: // left top
      glTranslatef(
	  0,
	  (bb[4] - bb[1])/-1.0,
	  0);
      break;
  }

  glTranslatef(x * (500.0*SCALE) , y * (500.0*SCALE), z * SCALE);
  ftglRenderFont(ui->font_small, text, FTGL_RENDER_ALL);
  glPopMatrix();
}

static void
unity_box2d(PuglView* view,
    const float x0, const float x1,
    const float y0, const float y1,
    const float z,
    const GLfloat color[4])
{
  glPushMatrix();
  glLoadIdentity();
  glScalef(SCALE, SCALE, SCALE);
  glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, color);
  glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, color);
  glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, color);
  glBegin(GL_QUADS);
  glVertex3f(x0, y0, z);
  glVertex3f(x1, y0, z);
  glVertex3f(x1, y1, z);
  glVertex3f(x0, y1, z);
  glEnd();
  glPopMatrix();
}

/******************************************************************************
 * puGL callbacks
 */

static void
onReshape(PuglView* view, int width, int height)
{
  BLCui* ui = (BLCui*)puglGetHandle(view);
  const float invaspect = (float) height / (float) width;

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(-1.0, 1.0, -invaspect, invaspect, 3.0, -3.0);

  glRotatef(ui->rot[0], 0, 1, 0);
  glRotatef(ui->rot[1], 1, 0, 0);
  glRotatef(ui->rot[2], 0, 0, 1);
  glScalef(ui->scale, ui->scale, ui->scale);
  glTranslatef(ui->off[0], ui->off[1], ui->off[2]);

  GLdouble matrix[16];
  glGetDoublev(GL_PROJECTION_MATRIX, matrix);
  invertMatrix(matrix, ui->matrix);

#ifdef DEBUG_ROTATION_MATRIX
  print4x4(matrix);
  print4x4(ui->matrix);
#endif

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  return;
}



/* main display function */
static void
onDisplay(PuglView* view)
{
  int i;
  char tval[16];
  BLCui* ui = (BLCui*)puglGetHandle(view);

  const GLfloat no_mat[] = { 0.0, 0.0, 0.0, 1.0 };
  const GLfloat mat_specular[] = { 1.0, 1.0, 1.0, 1.0 };
  const GLfloat no_shininess[] = { 128.0 };
  const GLfloat high_shininess[] = { 5.0 };

  const GLfloat mat_strip[] =  { 0.05, 0.05, 0.05, 1.0 };
  const GLfloat mat_dial[] =   { 0.10, 0.10, 0.10, 1.0 };
  const GLfloat mat_button[] = { 0.10, 0.10, 0.10, 1.0 };
  const GLfloat mat_switch[] = { 1.0, 1.0, 0.94, 1.0 };
  const GLfloat glow_red[] =   { 1.0, 0.0, 0.00, 0.3 };
  const GLfloat lamp_red[] =   { 0.5, 0.0, 0.00, 1.0 };

  if (!ui->initialized) {
    /* initialization needs to happen from event context
     * after pugl set glXMakeCurrent() - this /should/ otherwise
     * be done during initialization()
     */
    ui->initialized = 1;
    setupOpenGL();
    initMesh(ui->view);
    setupLight();
    initTextures(ui->view);
    ui->font_small = ftglCreateBufferFont(FONTFILE);
    ftglSetFontFaceSize(ui->font_small, FONTSIZE, 72);
    ftglSetFontCharMap(ui->font_small, ft_encoding_unicode);
  }

  /** step 1 - draw background -- fixed objects **/
#if 1
  glPushMatrix();
  glLoadIdentity();
  const float bgaspect = (float) ui->height / (float) ui->width / 2.0;
  glScalef(SCALE, SCALE * bgaspect, SCALE);

  glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, no_mat);
  glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mat_strip);
  glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, no_mat);
  glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, no_shininess);
  glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, no_mat);

  glEnable(GL_TEXTURE_2D);
  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
  glBindTexture(GL_TEXTURE_2D, ui->texID[0]);

  drawMesh(view, OBJ_CSTRIP);

  glDisable(GL_TEXTURE_2D);
  glPopMatrix();
#endif
  /** step 2 - draw /movable/ objects **/

  /* base material of moveable objects */
  glMaterialfv(GL_FRONT_AND_BACK, GL_SHININESS, high_shininess);
  glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, mat_specular);

  for (i = 0; i < TOTAL_OBJ; ++i) {

    glPushMatrix();
    glLoadIdentity();
    glScalef(SCALE, SCALE, SCALE);
    glTranslatef(ui->ctrls[i].x, ui->ctrls[i].y, 0.0f);

    switch(ui->ctrls[i].type) {
      case OBJ_DIAL:
	glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, mat_dial);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mat_dial);
	glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, no_mat);
	if (ui->ctrls[i].max == 0) {
	  glRotatef(
	      180 - (360.0 * rint(ui->ctrls[i].cur - ui->ctrls[i].min) / (1.0 + ui->ctrls[i].max - ui->ctrls[i].min))
	      , 0, 0, 1);
	} else {
	  glRotatef(
	      330.0 - (300.0 * (ui->ctrls[i].cur - ui->ctrls[i].min) / (ui->ctrls[i].max - ui->ctrls[i].min))
	      , 0, 0, 1);
	}
	break;
      case OBJ_SWITCH:
	glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, mat_switch);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mat_switch);
	if (ui->ctrls[i].cur == ui->ctrls[i].max) {
	  glMaterialfv(GL_FRONT, GL_EMISSION, glow_red);
	} else {
	  glMaterialfv(GL_FRONT, GL_EMISSION, no_mat);
	}
	glRotatef(ui->ctrls[i].cur == ui->ctrls[i].min ? -12 : 12.0, 1, 0, 0);
	break;
      case OBJ_PUSHBUTTON:
	glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, mat_button);
	glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, mat_button);
	if (ui->ctrls[i].cur == ui->ctrls[i].max) {
	  glMaterialfv(GL_FRONT, GL_EMISSION, lamp_red);
	  glTranslatef(0.0, 0.0, .38);
	} else {
	  glTranslatef(0.0, 0.0, .15);
	  glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, no_mat);
	}
	break;
      default:
	break;
    }

    if (i == ui->hoverid) {
      glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, glow_red);
    }

    if (ui->ctrls[i].texID > 0) {
      glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
      glEnable(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D, ui->texID[ui->ctrls[i].texID]);
    }

    drawMesh(view, ui->ctrls[i].type);
    glDisable(GL_TEXTURE_2D);

    glPopMatrix();

    if (ui->ctrls[i].type == OBJ_DIAL && ui->ctrls[i].max != 0) {
      float x = ui->ctrls[i].x;
      float y = ui->ctrls[i].y - ui->ctrls[i].h + .2;
      // TODO -- format callback function
      if (i==0) {
	/* trim */
	sprintf(tval, "%+02.1fdB", ui->ctrls[i].cur);
      } else if (i==1) {
	/* balance */
	int p= rint(ui->ctrls[i].cur * 100);
	if (p < 0) {
	  sprintf(tval, "L%3d", -p);
	} else if (ui->ctrls[i].cur > 0) {
	  sprintf(tval, "R%3d", p);
	} else {
	  sprintf(tval, "center");
	}
      } else {
	sprintf(tval, "%.0fsm", ui->ctrls[i].cur);
      }
      unity_box2d(view, x-1.0, x+1.0, y-0.3, y+0.3, 0, no_mat);
      render_text(view, tval, x, y, -0.1f, 1);
    }
  }

  /* value info */
  sprintf(tval, "%+02.1fdB", ui->p_bal[0]);
  render_text(view, tval, -1.35, ui->ctrls[1].y + 1.1, -0.1f, 1);

  sprintf(tval, "%+02.1fdB", ui->p_bal[1]);
  render_text(view, tval,  1.30, ui->ctrls[1].y + 1.1, -0.1f, 1);

  sprintf(tval, "%.1fms", ui->p_dly[0]);
  render_text(view, tval, -1.35, ui->ctrls[3].y - .3, -0.1f, 1);

  sprintf(tval, "%.1fms", ui->p_dly[1]);
  render_text(view, tval,  1.30, ui->ctrls[3].y - .3, -0.1f, 1);

  if (1) {
    /* meters */
    float x = -3.5;
    float y =  5.4;

    unity_box2d(view, x-.12, x+.12, y, y + 3.50, 0, no_mat);
    unity_box2d(view, x-.10, x+.10, y, y + 3.50 * ui->p_mtr_in[0], -.1, glow_red);

    x = -3.2;
    unity_box2d(view, x-.12, x+.12, y, y + 3.50, 0, no_mat);
    unity_box2d(view, x-.10, x+.10, y, y + 3.50 * ui->p_mtr_in[1], -.1, glow_red);

    y = -8.8;
    x = -3.5;
    unity_box2d(view, x-.12, x+.12, y, y + 3.50, 0, no_mat);
    unity_box2d(view, x-.10, x+.10, y, y + 3.50 * ui->p_mtr_out[0], -.1, glow_red);

    x = -3.2;
    unity_box2d(view, x-.12, x+.12, y, y + 3.50, 0, no_mat);
    unity_box2d(view, x-.10, x+.10, y, y + 3.50 * ui->p_mtr_out[1], -.1, glow_red);
  }

  if (1) {
    switch((int) vmap_val(view, 2)) {
      case 1:
	render_text(view, "maintain",   -3.0, 2.4, -0.1f, 4);
	render_text(view, "amplitude",  -3.0, 2.0, -0.1f, 4);
	break;
      case 2:
	render_text(view, "equal",  -3.0, 2.4, -0.1f, 4);
	render_text(view, "power",  -3.0, 2.0, -0.1f, 4);
	break;
      default:
	render_text(view, "classic",  -3.0, 2.4, -0.1f, 4);
	render_text(view, "balance",  -3.0, 2.0, -0.1f, 4);
	break;
    }
  }
}

static void
onKeyboard(PuglView* view, bool press, uint32_t key)
{
  BLCui* ui = (BLCui*)puglGetHandle(view);
  int queue_reshape = 0;

  if (!press) {
    return;
  }

  switch (key) {
    case 'a':
      if (ui->rot[0] > -45) { ui->rot[0] -= 5; queue_reshape = 1; }
      break;
    case 'd':
      if (ui->rot[0] <  45) { ui->rot[0] += 5; queue_reshape = 1; }
      break;
    case 'x':
      if (ui->rot[1] >   0) { ui->rot[1] -= 5; queue_reshape = 1; }
      break;
    case 'w':
      if (ui->rot[1] <  45) { ui->rot[1] += 5; queue_reshape = 1; }
      break;
    case 'z':
      if (ui->rot[2] > -30) { ui->rot[2] -= 5; queue_reshape = 1; }
      break;
    case 'c':
      if (ui->rot[2] <  30) { ui->rot[2] += 5; queue_reshape = 1; }
      break;
    case '+':
      if (ui->scale < 1.5)  { ui->scale += .025; queue_reshape = 1; }
      break;
    case '-':
      if (ui->scale > 0.6)  { ui->scale -= .025; queue_reshape = 1; }
      break;
    case 'h':
      if (ui->off[0] > -.5) { ui->off[0] -= .025; queue_reshape = 1; }
      break;
    case 'l':
      if (ui->off[0] <  .5) { ui->off[0] += .025; queue_reshape = 1; }
      break;
    case 'j':
      if (ui->off[1] > -.5) { ui->off[1] -= .025; queue_reshape = 1; }
      break;
    case 'k':
      if (ui->off[1] <  .5) { ui->off[1] += .025; queue_reshape = 1; }
      break;
    case 's':
      ui->scale = 1.0;
      ui->rot[0] = ui->rot[1] = ui->rot[2] = 0.0;
      ui->off[0] = ui->off[1] = ui->off[2] = 0.0;
      queue_reshape = 1;
      break;
    case 'e':
      ui->scale = 1.0;
      ui->rot[0] = 0;
      ui->rot[1] = 10;
      ui->rot[2] = 0;
      ui->off[0] = ui->off[1] = ui->off[2] = 0.0;
      queue_reshape = 1;
      break;
    default:
      break;
  }

  if (queue_reshape) {
    onReshape(view, ui->width, ui->height);
    puglPostRedisplay(view);
  }

  return;
}

static void
onScroll(PuglView* view, int x, int y, float dx, float dy)
{
  BLCui* ui = (BLCui*)puglGetHandle(view);
  float fx, fy;
  project_mouse(view, x, y, &fx, &fy);
  int i;
  for (i = 0; i < TOTAL_OBJ ; ++i) {
    if (MOUSEOVER(ui->ctrls[i], fx, fy)) {
      if (ui->ctrls[i].max == 0) {
	/* fixed integer dials */
	ui->dndval = ui->ctrls[i].cur + SIGNUM(dy) / (ui->ctrls[i].max - ui->ctrls[i].min);
      } else if ((ui->ctrls[i].max - ui->ctrls[i].min) <= 2) {
	/* -1..+1 float dial */
	ui->dndval = ui->ctrls[i].cur + SIGNUM(dy) * .01;
      } else {
	ui->dndval = ui->ctrls[i].cur + SIGNUM(dy);
      }
      processMotion(view, i, 0, 0);
      break;
    }
  }
}

static void
onMotion(PuglView* view, int x, int y)
{
  BLCui* ui = (BLCui*)puglGetHandle(view);
  float fx, fy;
  project_mouse(view, x, y, &fx, &fy);

  if (ui->dndid < 0) {
    int hover = ui->hoverid;
    ui->hoverid = -1;
    for (int i = 0; i < TOTAL_OBJ; ++i) {
      if (!MOUSEOVER(ui->ctrls[i], fx, fy)) {
	continue;
      }
      ui->hoverid = i;
      break;
    }
    if (hover != ui->hoverid) {
      puglPostRedisplay(view);
    }
    return;
  }

  processMotion(view, ui->dndid, fx - ui->dndx, fy - ui->dndy);
}

static void
onMouse(PuglView* view, int button, bool press, int x, int y)
{
  BLCui* ui = (BLCui*)puglGetHandle(view);
  int i;
  float fx, fy;

  ui->dndid = -1;

  if (!press) {
    return;
  }

  project_mouse(view, x, y, &fx, &fy);

  if (puglGetModifiers(view) & PUGL_MOD_CTRL) {
    ui->dndscale =.05;
  } else if (puglGetModifiers(view) & (PUGL_MOD_ALT | PUGL_MOD_SUPER)) {
    ui->dndscale = 2.0;
  } else {
    ui->dndscale = 1.0;
  }

  for (i = 0; i < TOTAL_OBJ; ++i) {
    if (!MOUSEOVER(ui->ctrls[i], fx, fy)) {
      continue;
    }
    switch (ui->ctrls[i].type) {
      case OBJ_DIAL:
	if (puglGetModifiers(view) & PUGL_MOD_SHIFT) {
	  ui->ctrls[i].cur = ui->ctrls[i].dfl;
	  notifyPlugin(view, i);
	  puglPostRedisplay(view);
	} else {
	  ui->dndid = i;
	  ui->dndx = fx;
	  ui->dndy = fy;
	  ui->dndval = ui->ctrls[i].cur;
	}
	break;
      case OBJ_SWITCH:
	if (ui->ctrls[i].cur == ui->ctrls[i].max)
	  ui->ctrls[i].cur = ui->ctrls[i].min;
	else
	  ui->ctrls[i].cur = ui->ctrls[i].max;
	notifyPlugin(view, i);
	puglPostRedisplay(view);
	break;
      case OBJ_PUSHBUTTON:
	notifyPlugin(view, i);
	puglPostRedisplay(view);
	break;
      default:
	break;
    }
    break;
  }
}


/******************************************************************************
 * misc - used for LV2 init/operation
 */

#ifdef OLD_SUIL
static void* ui_thread(void* ptr)
{
  BLCui* ui = (BLCui*)ptr;
  while (!ui->exit) {
    usleep(1000000 / 25);  // 25 FPS
    puglProcessEvents(ui->view);
  }
  return NULL;
}
#else
static int idle(LV2UI_Handle handle) {
  BLCui* ui = (BLCui*)handle;
  puglProcessEvents(ui->view);
  return 0;
}
#endif


/******************************************************************************
 * main GUI setup
 */

static int blc_gui_setup(BLCui* ui, const LV2_Feature* const* features) {
  PuglNativeWindow parent = 0;
  LV2UI_Resize*    resize = NULL;
  int i;

  ui->width       = 310;
  ui->height      = 620;
  ui->initialized = 0;

  ui->rot[0]     =  0.0;
  ui->rot[1]     =  10.0;
  ui->rot[2]     =  0.0;
  ui->scale      =  1.0;
  ui->off[0]     =  0.0f;
  ui->off[1]     =  0.0f;
  ui->off[2]     =  0.0f;

  ui->dndid      = -1;
  ui->hoverid    = -1;
  ui->dndscale   = 1.0;
  ui->dndval     = 0.0;
  ui->dndx       = 0.0;
  ui->dndy       = 0.0;

  ui->p_bal[0] = ui->p_bal[1] = 0;
  ui->p_dly[0] = ui->p_dly[1] = 0;
  ui->p_mtr_in[0] = ui->p_mtr_in[1] = 0;
  ui->p_mtr_out[0] = ui->p_mtr_out[1] = 0;

  for (i = 0; features && features[i]; ++i) {
    if (!strcmp(features[i]->URI, LV2_UI__parent)) {
      parent = (PuglNativeWindow)features[i]->data;
    } else if (!strcmp(features[i]->URI, LV2_UI__resize)) {
      resize = (LV2UI_Resize*)features[i]->data;
    }
  }

  if (!parent) {
    fprintf(stderr, "BLCLv2UI error: No parent window provided.\n");
    return -1;
  }

  /* prepare meshes */
  ui->vbo = (GLuint *)malloc(OBJECTS_COUNT * sizeof(GLuint));
  ui->vinx = (GLuint *)malloc(OBJECTS_COUNT * sizeof(GLuint));

  /* Set up GL UI */
  ui->view = puglCreate(parent, "Stereo Balance Control", ui->width, ui->height, resize ? true : false);
  puglSetHandle(ui->view, ui);
  puglSetDisplayFunc(ui->view, onDisplay);
  puglSetReshapeFunc(ui->view, onReshape);
  puglSetKeyboardFunc(ui->view, onKeyboard);
  puglSetMotionFunc(ui->view, onMotion);
  puglSetMouseFunc(ui->view, onMouse);
  puglSetScrollFunc(ui->view, onScroll);

  if (resize) {
    resize->ui_resize(resize->handle, ui->width, ui->height);
  }

  /** add control elements **/
  const float invaspect = (float) ui->height / (float) ui->width;

#define CTRLELEM(ID, TYPE, VMIN, VMAX, VCUR, PX, PY, W, H, TEXID) \
  {\
    ui->ctrls[ID].type = TYPE; \
    ui->ctrls[ID].min = VMIN; \
    ui->ctrls[ID].max = VMAX; \
    ui->ctrls[ID].cur = VCUR; \
    ui->ctrls[ID].dfl = VCUR; \
    ui->ctrls[ID].x = PX; \
    ui->ctrls[ID].y = PY * invaspect; \
    ui->ctrls[ID].w = W; \
    ui->ctrls[ID].h = H; \
    ui->ctrls[ID].texID = TEXID; \
  }

  CTRLELEM(0, OBJ_DIAL, -20, 20, 0,     3.0,  3.7,  1.5, 1.5, 1); // trim

  CTRLELEM(1, OBJ_DIAL, -1, 1, 0,         0,  1.2,  1.5, 1.5, 1); // balance
  CTRLELEM(2, OBJ_DIAL,  -2, 0, -2,     3.0,  1.2,  1.5, 1.5, 1); // mode

  CTRLELEM(3, OBJ_DIAL,  0, 2000, 0,  -3.05, -1.1,  1.5, 1.5, 1);
  CTRLELEM(4, OBJ_DIAL,  0, 2000, 0,    3.0, -1.1,  1.5, 1.5, 1);

  CTRLELEM(5, OBJ_PUSHBUTTON, 0, 1, 0, -2.2, -3.9,  1.0, 1.0, -1); 
  CTRLELEM(6, OBJ_PUSHBUTTON, 0, 1, 0, -0.8, -3.9,  1.0, 1.0, -1); 
  CTRLELEM(7, OBJ_PUSHBUTTON, 0, 1, 0,  .65, -3.9,  1.0, 1.0, -1); 
  CTRLELEM(8, OBJ_PUSHBUTTON, 0, 1, 0,  2.1, -3.9,  1.0, 1.0, -1); 
  CTRLELEM(9, OBJ_PUSHBUTTON, 0, 1, 0,  3.5, -3.9,  1.0, 1.0, -1); 

#ifdef OLD_SUIL
  ui->exit = false;
  pthread_create(&ui->thread, NULL, ui_thread, ui);
#endif

  return 0;
}

/******************************************************************************
 * LV2 callbacks
 */

static LV2UI_Handle
instantiate(const LV2UI_Descriptor*   descriptor,
            const char*               plugin_uri,
            const char*               bundle_path,
            LV2UI_Write_Function      write_function,
            LV2UI_Controller          controller,
            LV2UI_Widget*             widget,
            const LV2_Feature* const* features)
{
  if (strcmp(plugin_uri, BLC_URI)) {
    fprintf(stderr, "This GUI does not support plugin with URI %s\n", plugin_uri);
    return NULL;
  }

  BLCui* ui = (BLCui*)malloc(sizeof(BLCui));

  ui->write      = write_function;
  ui->controller = controller;

  for (int i = 0; features[i]; ++i) {
    if (!strcmp(features[i]->URI, LV2_URID__map)) {
      ui->map = (LV2_URID_Map*)features[i]->data;
    }
  }

  if (!ui->map) {
    fprintf(stderr, "BLClv2 error: Host does not support urid:map\n");
    free(ui);
    return NULL;
  }

  map_balance_uris(ui->map, &ui->uris);


  if (blc_gui_setup(ui, features)) {
    free(ui);
    return NULL;
  }

  *widget = (void*)puglGetNativeWindow(ui->view);

  return ui;
}

static void
cleanup(LV2UI_Handle handle)
{
  BLCui* ui = (BLCui*)handle;
#ifdef OLD_SUIL
  ui->exit = true;
  pthread_join(ui->thread, NULL);
#endif
  ftglDestroyFont(ui->font_small);
  puglDestroy(ui->view);
  free(ui->vbo);
  free(ui->vinx);
  free(ui);
}

static void
port_event(LV2UI_Handle handle,
    uint32_t     port_index,
    uint32_t     buffer_size,
    uint32_t     format,
    const void*  buffer)
{
  BLCui* ui = (BLCui*)handle;

  if ( format == 0 ) {
    if (port_index < 0 || port_index >= TOTAL_OBJ) return;
    float value =  *(float *)buffer;
    rmap_val(ui->view, port_index, value);
    puglPostRedisplay(ui->view);
    return;
  }
  if (format != ui->uris.atom_eventTransfer) {
    return;
  }

  LV2_Atom* atom = (LV2_Atom*)buffer;
  if (atom->type != ui->uris.atom_Blank) {
    return;
  }

  char *k; float v;
  if (get_cc_key_value(&ui->uris, (LV2_Atom_Object*)atom, &k, &v)) {
    return;
  }

  //printf("key: '%s' val: %f\n", k, v);

  if      (!strcmp(k, "gain_left"))   { ui->p_bal[0] = v; }
  else if (!strcmp(k, "gain_right"))  { ui->p_bal[1] = v; }
  else if (!strcmp(k, "delay_left"))  { ui->p_dly[0] = v * 1000.0; }
  else if (!strcmp(k, "delay_right")) { ui->p_dly[1] = v * 1000.0; }
  else if (!strcmp(k, "meter_inl"))   { ui->p_mtr_in[0] = v * 0.01; }
  else if (!strcmp(k, "meter_inr"))   { ui->p_mtr_in[1] = v * 0.01; }
  else if (!strcmp(k, "meter_outl"))  { ui->p_mtr_out[0] = v * 0.01; }
  else if (!strcmp(k, "meter_outr"))  { ui->p_mtr_out[1] = v * 0.01; }
  else return;

  puglPostRedisplay(ui->view);
}

/******************************************************************************
 * LV2 setup
 */

#ifndef OLD_SUIL
static const LV2UI_Idle_Interface idle_iface = { idle };
#endif

static const void*
extension_data(const char* uri)
{
#ifndef OLD_SUIL
  if (!strcmp(uri, LV2_UI__idleInterface)) {
    return &idle_iface;
  }
#endif
  return NULL;
}

static const LV2UI_Descriptor descriptor = {
  BLC_URI "#ui",
  instantiate,
  cleanup,
  port_event,
  extension_data
};

LV2_SYMBOL_EXPORT
const LV2UI_Descriptor*
lv2ui_descriptor(uint32_t index)
{
  switch (index) {
  case 0:
    return &descriptor;
  default:
    return NULL;
  }
}

/* vi:set ts=8 sts=2 sw=2: */
