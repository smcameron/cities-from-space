#ifndef __BLINE_H
#define __BLINE_H
typedef void (*plotting_function)(int x, int y, void *context);

extern void bline(int x1, int y1, int x2, int y2, plotting_function plot_func, void *context);

#endif
