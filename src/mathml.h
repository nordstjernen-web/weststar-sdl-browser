/* Weststar SDL Browser — measures and paints a <math> subtree for layout and paint.
 * Copyright 2026 Andreas Røsdal
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef NS_MATHML_H
#define NS_MATHML_H

#include "gfx.h"
#include <glib.h>

#include "dom.h"

G_BEGIN_DECLS

void ns_math_measure(const ns_node *math, double font_px,
                     double *out_w, double *out_ascent, double *out_descent);

void ns_math_paint(gfx_t *cr, const ns_node *math, double x, double y,
                   double font_px, double r, double g, double b, double a);

G_END_DECLS

#endif
