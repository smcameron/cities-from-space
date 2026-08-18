#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <getopt.h>

#include "bline.h"
#include "png_utils.h"

int img_size = 512;

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

/* Prints usage information and exits */
void usage(const char *prog_name) {
	fprintf(stderr, "Usage: %s [options]\n", prog_name);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -s, --size <pixels>   Set the image dimensions (must be square). Default is 512.\n");
	fprintf(stderr, "  -h, --help            Show this help message.\n");
	exit(EXIT_FAILURE);
}

/* Simple separable box blur for an 8-bit alpha mask */
void blur_alpha_mask(unsigned char *src, unsigned char *dst, int radius, int passes) {
	int w = img_size;
	int h = img_size;
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

/* Separable box blur for 4-channel RGBA images to create a glow effect */
/* Output is src.  dst is temporary working space */
void blur_rgba_image(unsigned char *src, unsigned char *dst, int w, int h, int radius, int passes) {
	unsigned char *temp = (unsigned char *)malloc(w * h * 4);

	for (int p = 0; p < passes; p++) {
		/* Horizontal pass */
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				int r_sum = 0, g_sum = 0, b_sum = 0, a_sum = 0, count = 0;
				for (int k = -radius; k <= radius; k++) {
					int nx = x + k;
					if (nx >= 0 && nx < w) {
						int idx = (y * w + nx) * 4;
						r_sum += src[idx];
						g_sum += src[idx + 1];
						b_sum += src[idx + 2];
						a_sum += src[idx + 3];
						count++;
					}
				}
				int out_idx = (y * w + x) * 4;
				temp[out_idx]     = r_sum / count;
				temp[out_idx + 1] = g_sum / count;
				temp[out_idx + 2] = b_sum / count;
				temp[out_idx + 3] = a_sum / count;
			}
		}
		/* Vertical pass */
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				int r_sum = 0, g_sum = 0, b_sum = 0, a_sum = 0, count = 0;
				for (int k = -radius; k <= radius; k++) {
					int ny = y + k;
					if (ny >= 0 && ny < h) {
						int idx = (ny * w + x) * 4;
						r_sum += temp[idx];
						g_sum += temp[idx + 1];
						b_sum += temp[idx + 2];
						a_sum += temp[idx + 3];
						count++;
					}
				}
				int out_idx = (y * w + x) * 4;
				dst[out_idx]     = r_sum / count;
				dst[out_idx + 1] = g_sum / count;
				dst[out_idx + 2] = b_sum / count;
				dst[out_idx + 3] = a_sum / count;
			}
		}
		/* Copy dst back to src for the next pass */
		for (int i = 0; i < w * h * 4; i++) {
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
			if (x < 0 || x >= img_size || y < 0 || y >= img_size) continue;

			if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) {
				/* Sample color from the city texture mapping coordinates */
				int tx = ((x % tex->w) + tex->w) % tex->w;
				int ty = ((y % tex->h) + tex->h) % tex->h;
				int t_idx = (ty * tex->w + tx) * (tex->hasAlpha ? 4 : 3);
				int d_idx = (y * img_size + x) * 4;

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
			if (px < 0 || px >= img_size || py < 0 || py >= img_size) continue;

			/* Retrieve the road's opacity directly from the blurred alpha mask */
			unsigned char road_alpha = ctx->blurred_alpha[py * img_size + px];

			/* If the road is completely invisible and outside the city, skip drawing entirely */
			if (road_alpha == 0) continue;

			int idx = (py * img_size + px) * 4;

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

/* Adds a bloom effect to img */
static void add_bloom_effect(unsigned char *img, int w, int h)
{
	int img_bytes = w * h * 4;
	unsigned char *temp_space, *orig_img;

	/* Save copy of original img */
	orig_img = malloc(img_bytes);
	memcpy(orig_img, img, img_bytes);

	/* Blur img */
	temp_space = malloc(img_bytes);
	blur_rgba_image(img, temp_space, img_size, img_size, 2, 2);
	free(temp_space);

	/* Combine img with orig_img */
	for (int i = 0; i < img_bytes; i += 4) {
		unsigned char *sharp = &orig_img[i];
		unsigned char *blur  = &img[i];

		int r = (sharp[0] + blur[0]) / 2;
		int g = (sharp[1] + blur[1]) / 2;
		int b = (sharp[2] + blur[2]) / 2;

		blur[0] = (r > 255) ? 255 : (unsigned char)r;
		blur[1] = (g > 255) ? 255 : (unsigned char)g;
		blur[2] = (b > 255) ? 255 : (unsigned char)b;

		/* Alpha composition: A + B - (A * B) / 255 */
		int a_sharp = sharp[3];
		int a_blur  = blur[3];
		int a_out   = a_sharp + a_blur - (a_sharp * a_blur) / 255;

		blur[3] = (unsigned char)a_out;
	}
}

static void draw_small_grid_roads(PlotContext *ctx, int w, int h, int spacing)
{
	int grid_spacing = spacing;
	/* Small roads in a semi-regular grid */
	ctx->thickness = 1;
	for (int y = grid_spacing; y < h; y += grid_spacing + (rand() % 5 - 2)) {
		if (rand() % 1000 < 300) /* skip some grid lines */
			continue;
		int startx, stopx;
		if ((rand() % 1000) < 333) {
			startx = 0;
			stopx = img_size;
		} else {
			startx = rand() % (img_size / 3);
			stopx = rand() % (img_size / 3) + img_size / 2;
		}
		bline(startx, y, stopx, y + (rand() % 10 - 5), plot_road_pixel, ctx);
	}
	for (int x = grid_spacing; x < w; x += grid_spacing + (rand() % 5 - 2)) {
		if (rand() % 1000 < 300) /* skip some grid lines */
			continue;
		int starty, stopy;
		if ((rand() % 1000) < 333) {
			starty = 0;
			stopy = img_size;
		} else {
			starty = rand() % (img_size / 3);
			stopy = rand() % (img_size / 3) + img_size / 2;
		}
		bline(x, starty, x + (rand() % 10 - 5), stopy, plot_road_pixel, ctx);
	}
}

static void draw_main_artery_roads(PlotContext *ctx, int num_arteries, int w, int h)
{
	/* Thicker main artery roads traversing the image using midpoint displacement */
	for (int i = 0; i < num_arteries; i++) {
		int x1 = 0, y1 = rand() % w;
		int x2 = img_size, y2 = rand() % w;
		draw_md_line(x1, y1, x2, y2, 4, 30.0f, ctx);

		int x3 = rand() % h, y3 = 0;
		int x4 = rand() % h, y4 = h;
		draw_md_line(x3, y3, x4, y4, 4, 30.0f, ctx);
	}
}

static void draw_loop_road(PlotContext *ctx, int w, int h)
{
	int num_vertices = 8;
	int m = w < h ? w : h;
	int loop_radius = m / 3;
	int loop_cx = w / 2;
	int loop_cy = h / 2;

	int pts_x[8], pts_y[8];
	for (int i = 0; i < num_vertices; i++) {
		float angle = (2.0f * 3.14159265f * i) / num_vertices;
		/* Slightly squash and jitter the points */
		pts_x[i] = loop_cx + (int)(loop_radius * cos(angle) * (1.0f + (rand()%20 - 10)/100.0f));
		pts_y[i] = loop_cy + (int)(loop_radius * sin(angle) * (0.8f + (rand()%20 - 10)/100.0f));
	}
	for (int i = 0; i < num_vertices; i++) {
		int next_i = (i + 1) % num_vertices;
		draw_md_line(pts_x[i], pts_y[i], pts_x[next_i], pts_y[next_i], 3, 15.0f, ctx);
	}
}

static void draw_roads(PlotContext *ctx, int num_arteries, int grid_road_spacing, int w, int h)
{
	/* Small roads in a semi-regular grid */
	ctx->thickness = 1;
	draw_small_grid_roads(ctx, w, h, grid_road_spacing);

	/* Thicker main artery roads */
	ctx->thickness = 3;
	draw_main_artery_roads(ctx, num_arteries, w, h);

	/* A loop road */
	ctx->thickness = 2;
	draw_loop_road(ctx, w, h);
}

int main(int argc, char *argv[]) {
	int opt;

	/* Setup getopt_long for command line parsing */
	static struct option long_options[] = {
		{"size", required_argument, 0, 's'},
		{"help", no_argument,       0, 'h'},
		{0, 0, 0, 0}
	};

	while ((opt = getopt_long(argc, argv, "s:h", long_options, NULL)) != -1) {
		switch (opt) {
			case 's':
				img_size = atoi(optarg);
				if (img_size <= 0) {
					fprintf(stderr, "Error: Image size must be a positive integer.\n");
					usage(argv[0]);
				}
				break;
			case 'h':
			case '?':
			default:
				usage(argv[0]);
		}
	}

	srand((unsigned int)time(NULL));

	/* Initialize texture structures */
	Texture city_tex, road_tex, light_tex;

	if (!load_texture("city-texture.png", &city_tex) ||
		!load_texture("road-texture.png", &road_tex) ||
		!load_texture("lights-texture.png", &light_tex)) {
		return 1;
	}

	/* Start with all transparent images for the maps */
	int img_bytes = img_size * img_size * 4;
	unsigned char *diffuse_map = (unsigned char *)calloc(img_bytes, 1);
	unsigned char *emittance_map = (unsigned char *)calloc(img_bytes, 1);

	/* Draw diffuse map recursive circles */
	float initial_radius = (0.3f * img_size);
	draw_recursive_circles(diffuse_map, img_size / 2, img_size / 2, initial_radius, &city_tex);

	/* Extract and blur the alpha mask */
	unsigned char *raw_alpha = (unsigned char *)malloc(img_size * img_size);
	unsigned char *blurred_alpha = (unsigned char *)malloc(img_size * img_size);

	for (int i = 0; i < img_size * img_size; i++) {
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

	draw_roads(&ctx, 3, 16, img_size, img_size);

	add_bloom_effect(emittance_map, img_size, img_size);

	png_utils_write_png_image("diffuse_map.png", diffuse_map, img_size, img_size, 1, 0);
	png_utils_write_png_image("emittance_map.png", emittance_map, img_size, img_size, 1, 0);

	free(diffuse_map);
	free(emittance_map);
	free(raw_alpha);
	free(blurred_alpha);
	free(city_tex.pixels);
	free(road_tex.pixels);
	free(light_tex.pixels);

	printf("City decals generated successfully at %dx%d resolution.\n", img_size, img_size);
	return 0;
}
