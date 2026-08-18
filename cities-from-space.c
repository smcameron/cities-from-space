#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "bline.h"
#include "png_utils.h"

#define IMG_SIZE 512

/* Structure to hold loaded texture data */
typedef struct {
	unsigned char *pixels;
	int w;
	int h;
	int hasAlpha;
} Texture;

/* Context passed to the bline plotting function */
typedef struct {
	unsigned char *diffuse;
	unsigned char *emittance;
	Texture *road_tex;
	Texture *light_tex;
	int thickness;
	unsigned char *blurred_alpha;
} PlotContext;

/* Simple separable box blur for an 8-bit alpha mask */
void blur_alpha_mask(unsigned char *src, unsigned char *dst, int radius, int passes) {
	int w = IMG_SIZE;
	int h = IMG_SIZE;
	unsigned char *temp = (unsigned char *)malloc(w * h);

	for (int p = 0; p < passes; p++) {
		/* Horizontal pass */
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				int sum = 0, count = 0;
				for (int k = -radius; k <= radius; k++) {
					int nx = x + k;
					if (nx >= 0 && nx < w) {
						sum += src[y * w + nx];
						count++;
					}
				}
				temp[y * w + x] = sum / count;
			}
		}
		/* Vertical pass */
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				int sum = 0, count = 0;
				for (int k = -radius; k <= radius; k++) {
					int ny = y + k;
					if (ny >= 0 && ny < h) {
						sum += temp[ny * w + x];
						count++;
					}
				}
				dst[y * w + x] = sum / count;
			}
		}
		/* Copy dst back to src for the next pass */
		for (int i = 0; i < w * h; i++) {
			src[i] = dst[i];
		}
	}
	free(temp);
}

/* Helper to load PNG textures */
int load_texture(const char *filename, Texture *tex) {
	char whynot[256];
	/* Using png_utils_read_png_image as specified */
	tex->pixels = (unsigned char *)png_utils_read_png_image(
		filename, 0, 0, 0, &tex->w, &tex->h, &tex->hasAlpha, whynot, sizeof(whynot)
	);
	if (!tex->pixels) {
		fprintf(stderr, "Failed to load %s: %s\n", filename, whynot);
		return 0;
	}
	return 1;
}

/* Recursive function to draw city diffuse base using circles */
void draw_recursive_circles(unsigned char *diffuse, int cx, int cy, float radius, Texture *tex) {
	/* Stop recursing when circles are a few pixels in diameter */
	if (radius < 3.0f) return;

	int r = (int)radius;
	for (int y = cy - r; y <= cy + r; y++) {
		for (int x = cx - r; x <= cx + r; x++) {
			if (x < 0 || x >= IMG_SIZE || y < 0 || y >= IMG_SIZE) continue;

			if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) {
				/* Sample color from the city texture mapping coordinates */
				int tx = ((x % tex->w) + tex->w) % tex->w;
				int ty = ((y % tex->h) + tex->h) % tex->h;
				int t_idx = (ty * tex->w + tx) * (tex->hasAlpha ? 4 : 3);
				int d_idx = (y * IMG_SIZE + x) * 4;

				diffuse[d_idx]     = tex->pixels[t_idx];
				diffuse[d_idx + 1] = tex->pixels[t_idx + 1];
				diffuse[d_idx + 2] = tex->pixels[t_idx + 2];
				diffuse[d_idx + 3] = 255; /* Solid alpha */
			}
		}
	}

	/* Recurse with a few smaller circles near the edges */
	int num_children = 7 + (rand() % 4);
	for (int i = 0; i < num_children; i++) {
		float angle = (rand() % 360) * 3.14159265f * 2.0f / 180.0f; /* Distribute randomly */

		/* Each smaller circle is between 25 and 45 percent of the parent size */
		float size_factor = 0.20f + ((rand() % 21) / 100.0f);
		float new_r = radius * size_factor;

		int new_cx = cx + (int)(1.2f * radius * cos(angle));
		int new_cy = cy + (int)(1.2f * radius * sin(angle));

		draw_recursive_circles(diffuse, new_cx, new_cy, new_r, tex);
	}
}

/* Custom plot function passed to bline */
void plot_road_pixel(int x, int y, void *context) {
	PlotContext *ctx = (PlotContext *)context;
	int t = ctx->thickness;
	int half_t = t / 2;

	/* Draw thickness by looping around the center pixel */
	for (int dy = -half_t; dy <= half_t + (t % 2); dy++) {
		for (int dx = -half_t; dx <= half_t + (t % 2); dx++) {
			int px = x + dx;
			int py = y + dy;

			/* Bounds check */
			if (px < 0 || px >= IMG_SIZE || py < 0 || py >= IMG_SIZE) continue;

			/* Retrieve the road's opacity directly from the blurred alpha mask */
			unsigned char road_alpha = ctx->blurred_alpha[py * IMG_SIZE + px];

			/* If the road is completely invisible and outside the city, skip drawing entirely */
			if (road_alpha == 0) continue;

			int idx = (py * IMG_SIZE + px) * 4;

			/* Both the diffuse map and the emittance map are updated */
			if (ctx->road_tex) {
				int tx = ((px % ctx->road_tex->w) + ctx->road_tex->w) % ctx->road_tex->w;
				int ty = ((py % ctx->road_tex->h) + ctx->road_tex->h) % ctx->road_tex->h;
				int t_idx = (ty * ctx->road_tex->w + tx) * (ctx->road_tex->hasAlpha ? 4 : 3);

				ctx->diffuse[idx]     = ctx->road_tex->pixels[t_idx];
				ctx->diffuse[idx + 1] = ctx->road_tex->pixels[t_idx + 1];
				ctx->diffuse[idx + 2] = ctx->road_tex->pixels[t_idx + 2];

				/* Alpha Blending Logic:
				   Take the MAXIMUM of the existing alpha (from the recursive circles)
				   and our new fading road alpha. */
				if (road_alpha > ctx->diffuse[idx + 3]) {
					ctx->diffuse[idx + 3] = road_alpha;
				}
			}

			/* Emittance map samples from the lights texture */
			if (ctx->light_tex) {
				int tx = ((px % ctx->light_tex->w) + ctx->light_tex->w) % ctx->light_tex->w;
				int ty = ((py % ctx->light_tex->h) + ctx->light_tex->h) % ctx->light_tex->h;
				int t_idx = (ty * ctx->light_tex->w + tx) * (ctx->light_tex->hasAlpha ? 4 : 3);

				ctx->emittance[idx]     = ctx->light_tex->pixels[t_idx];
				ctx->emittance[idx + 1] = ctx->light_tex->pixels[t_idx + 1];
				ctx->emittance[idx + 2] = ctx->light_tex->pixels[t_idx + 2];

				/* Apply the exact same maximum-alpha logic to the emittance map */
				if (road_alpha > ctx->emittance[idx + 3]) {
					ctx->emittance[idx + 3] = road_alpha;
				}
			}
		}
	}
}

/* Draws a line using midpoint displacement to create curves */
void draw_md_line(int x1, int y1, int x2, int y2, int depth, float displacement, PlotContext *ctx) {
	if (depth == 0) {
		bline(x1, y1, x2, y2, plot_road_pixel, ctx); /* Rely on bline implementation */
		return;
	}

	int xm = (x1 + x2) / 2;
	int ym = (y1 + y2) / 2;

	/* Apply random displacement */
	xm += (rand() % (int)(displacement * 2 + 1)) - (int)displacement;
	ym += (rand() % (int)(displacement * 2 + 1)) - (int)displacement;

	draw_md_line(x1, y1, xm, ym, depth - 1, displacement / 2.0f, ctx);
	draw_md_line(xm, ym, x2, y2, depth - 1, displacement / 2.0f, ctx);
}

int main() {
	srand((unsigned int)time(NULL));

	/* Initialize texture structures */
	Texture city_tex, road_tex, light_tex;

	if (!load_texture("city-texture.png", &city_tex) ||
		!load_texture("road-texture.png", &road_tex) ||
		!load_texture("lights-texture.png", &light_tex)) {
		return 1;
	}

	/* Start with all transparent images for the maps */
	int img_bytes = IMG_SIZE * IMG_SIZE * 4;
	unsigned char *diffuse_map = (unsigned char *)calloc(img_bytes, 1);
	unsigned char *emittance_map = (unsigned char *)calloc(img_bytes, 1);

	/* Draw diffuse map recursive circles */
	float initial_radius = (0.3f * IMG_SIZE);
	draw_recursive_circles(diffuse_map, IMG_SIZE / 2, IMG_SIZE / 2, initial_radius, &city_tex);

	/* Extract and blur the alpha mask */
	unsigned char *raw_alpha = (unsigned char *)malloc(IMG_SIZE * IMG_SIZE);
	unsigned char *blurred_alpha = (unsigned char *)malloc(IMG_SIZE * IMG_SIZE);

	for (int i = 0; i < IMG_SIZE * IMG_SIZE; i++) {
		raw_alpha[i] = diffuse_map[(i * 4) + 3];
	}

	/* Tweak the radius and passes to change the length and smoothness of the road fade */
	blur_alpha_mask(raw_alpha, blurred_alpha, 15, 3);

	/* Context for road drawing operations */
	PlotContext ctx;
	ctx.diffuse = diffuse_map;
	ctx.emittance = emittance_map;
	ctx.road_tex = &road_tex;
	ctx.light_tex = &light_tex;
	ctx.blurred_alpha = blurred_alpha;

	/* Small roads in a semi-regular grid */
	ctx.thickness = 1;
	int grid_spacing = 16;
	for (int y = grid_spacing; y < IMG_SIZE; y += grid_spacing + (rand() % 5 - 2)) {
		if (rand() % 1000 < 300) /* skip some grid lines */
			continue;
		int startx, stopx;
		if ((rand() % 1000) < 333) {
			startx = 0;
			stopx = IMG_SIZE;
		} else {
			startx = rand() % (IMG_SIZE / 3);
			stopx = rand() % (IMG_SIZE / 3) + IMG_SIZE / 2;
		}
		bline(startx, y, stopx, y + (rand() % 10 - 5), plot_road_pixel, &ctx);
	}
	for (int x = grid_spacing; x < IMG_SIZE; x += grid_spacing + (rand() % 5 - 2)) {
		if (rand() % 1000 < 300) /* skip some grid lines */
			continue;
		int starty, stopy;
		if ((rand() % 1000) < 333) {
			starty = 0;
			stopy = IMG_SIZE;
		} else {
			starty = rand() % (IMG_SIZE / 3);
			stopy = rand() % (IMG_SIZE / 3) + IMG_SIZE / 2;
		}
		bline(x, starty, x + (rand() % 10 - 5), stopy, plot_road_pixel, &ctx);
	}

	/* Thicker main artery roads traversing the image using midpoint displacement */
	ctx.thickness = 3;
	int num_arteries = 3;
	for (int i = 0; i < num_arteries; i++) {
		int x1 = 0, y1 = rand() % IMG_SIZE;
		int x2 = IMG_SIZE, y2 = rand() % IMG_SIZE;
		draw_md_line(x1, y1, x2, y2, 4, 30.0f, &ctx);

		int x3 = rand() % IMG_SIZE, y3 = 0;
		int x4 = rand() % IMG_SIZE, y4 = IMG_SIZE;
		draw_md_line(x3, y3, x4, y4, 4, 30.0f, &ctx);
	}

	/* A loop road defined by vertices (e.g. an octagon) using midpoint displacement */
	ctx.thickness = 2;
	int num_vertices = 8;
	int loop_radius = IMG_SIZE / 3;
	int loop_cx = IMG_SIZE / 2;
	int loop_cy = IMG_SIZE / 2;

	int pts_x[8], pts_y[8];
	for (int i = 0; i < num_vertices; i++) {
		float angle = (2.0f * 3.14159265f * i) / num_vertices;
		/* Slightly squash and jitter the points */
		pts_x[i] = loop_cx + (int)(loop_radius * cos(angle) * (1.0f + (rand()%20 - 10)/100.0f));
		pts_y[i] = loop_cy + (int)(loop_radius * sin(angle) * (0.8f + (rand()%20 - 10)/100.0f));
	}

	for (int i = 0; i < num_vertices; i++) {
		int next_i = (i + 1) % num_vertices;
		draw_md_line(pts_x[i], pts_y[i], pts_x[next_i], pts_y[next_i], 3, 15.0f, &ctx);
	}

	/* Output final PNG files using png_utils */
	png_utils_write_png_image("diffuse_map.png", diffuse_map, IMG_SIZE, IMG_SIZE, 1, 0);
	png_utils_write_png_image("emittance_map.png", emittance_map, IMG_SIZE, IMG_SIZE, 1, 0);

	/* Clean up memory */
	free(diffuse_map);
	free(emittance_map);
	free(city_tex.pixels);
	free(road_tex.pixels);
	free(light_tex.pixels);

	printf("City decals generated successfully.\n");
	return 0;
}
