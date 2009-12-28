/* redshift.c -- Main program source
   This file is part of Redshift.

   Redshift is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Redshift is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Redshift.  If not, see <http://www.gnu.org/licenses/>.

   Copyright (c) 2009  Jon Lund Steffensen <jonlst@gmail.com>
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <locale.h>

#include "solar.h"

#ifdef ENABLE_RANDR
# include "randr.h"
#endif

#ifdef ENABLE_VIDMODE
# include "vidmode.h"
#endif

#if !(defined(ENABLE_RANDR) || defined(ENABLE_VIDMODE))
# error "Either RANDR or VidMode must be enabled."
#endif


/* Bounds for parameters. */
#define MIN_LAT   -90.0
#define MAX_LAT    90.0
#define MIN_LON  -180.0
#define MAX_LON   180.0
#define MIN_TEMP   1000
#define MAX_TEMP  10000
#define MIN_GAMMA   0.1
#define MAX_GAMMA  10.0

/* Default values for parameters. */
#define DEFAULT_DAY_TEMP    5500
#define DEFAULT_NIGHT_TEMP  3700
#define DEFAULT_GAMMA        1.0

/* Angular elevation of the sun at which the color temperature
   transition period starts and ends (in degress).
   Transition during twilight, and while the sun is lower than
   3.0 degrees above the horizon. */
#define TRANSITION_LOW     SOLAR_CIVIL_TWILIGHT_ELEV
#define TRANSITION_HIGH    3.0


#define USAGE  \
	"Usage: %s -l LAT:LON -t DAY:NIGHT [OPTIONS...]\n"
#define HELP \
	USAGE \
	" Set color temperature of display according to time of day.\n" \
	"  -g R:G:B\tAdditional gamma correction to apply\n" \
	"  -h\t\tDisplay this help message\n" \
	"  -l LAT:LON\tYour current location\n" \
	"  -m METHOD\tMethod to use to set color temperature" \
	" (randr or vidmode)\n"				      \
	"  -o\t\tOne shot mode (do not continously adjust" \
	" color temperature)"					   \
	"  -r\t\tDisable initial temperature transition" \
	"  -s SCREEN\tX screen to apply adjustments to\n" \
	"  -t DAY:NIGHT\tColor temperature to set at daytime/night\n" \
	"  -v\t\tVerbose output\n"

/* DEGREE SIGN is Unicode U+00b0 */
#define DEG_CHAR  0xb0


static int
calculate_temp(double elevation, int temp_day, int temp_night,
	       int verbose)
{
	int temp = 0;
	if (elevation < TRANSITION_LOW) {
		temp = temp_night;
		if (verbose) printf("Period: Night\n");
	} else if (elevation < TRANSITION_HIGH) {
		/* Transition period: interpolate */
		float a = (TRANSITION_LOW - elevation) /
			(TRANSITION_LOW - TRANSITION_HIGH);
		temp = (1.0-a)*temp_night + a*temp_day;
		if (verbose) {
			printf("Period: Transition (%.2f%% day)\n", a*100);
		}
	} else {
		temp = temp_day;
		if (verbose) printf("Period: Daytime\n");
	}

	return temp;
}

static int
adjust_temperature(int screen_num, int use_randr, int temp, float gamma[3])
{
	/* Set color temperature */
	int failed = 0;
#ifdef ENABLE_RANDR
	if (use_randr) {
		/* Check RANDR extension. */
		int r = randr_check_extension();
		if (r < 0) {
			fprintf(stderr, "RANDR 1.3 extension is"
				" not available.\n");
			failed = 1;
		} else {
			r = randr_set_temperature(screen_num, temp, gamma);
			if (r < 0) {
				fprintf(stderr, "Unable to set color"
					" temperature with RANDR.\n");
				failed = 1;
			}
		}
	}
#endif

#ifdef ENABLE_VIDMODE
	if (!use_randr || failed) {
		failed = 0;
		/* Check VidMode extension */
		int r = vidmode_check_extension();
		if (r < 0) {
			fprintf(stderr, "VidMode extension is"
				" not available.\n");
			failed = 1;
		} else {
			r = vidmode_set_temperature(screen_num, temp, gamma);
			if (r < 0) {
				fprintf(stderr, "Unable to set color"
					" temperature with VidMode.\n");
				failed = 1;
			}
		}
	}
#endif

	if (failed) {
		fprintf(stderr, "Color temperature adjustment failed.\n");
		return -1;
	}

	return 0;
}


int
main(int argc, char *argv[])
{
	int r;

	/* Init locale for degree symbol. */
	char *loc = setlocale(LC_CTYPE, "");
	if (loc == NULL) {
		fprintf(stderr, "Unable to set locale.\n");
		exit(EXIT_FAILURE);
	}

	/* Initialize to defaults */
	float lat = NAN;
	float lon = NAN;
	int temp_day = DEFAULT_DAY_TEMP;
	int temp_night = DEFAULT_NIGHT_TEMP;
	float gamma[3] = { DEFAULT_GAMMA, DEFAULT_GAMMA, DEFAULT_GAMMA };
	int use_randr = 1;
	int screen_num = -1;
	int init_trans = 1;
	int one_shot = 0;
	int verbose = 0;
	char *s;

#ifndef ENABLE_RANDR
	/* Don't use RANDR if it has been disabled. */
	use_randr = 0;
#endif

	/* Parse arguments. */
	int opt;
	while ((opt = getopt(argc, argv, "g:hl:m:ors:t:v")) != -1) {
		switch (opt) {
		case 'g':
			s = strchr(optarg, ':');
			if (s == NULL) {
				/* Use value for all channels */
				float g = atof(optarg);
				gamma[0] = gamma[1] = gamma[2] = g;
			} else {
				/* Parse separate value for each channel */
				*(s++) = '\0';
				gamma[0] = atof(optarg); /* Red */
				char *g_s = s;
				s = strchr(s, ':');
				if (s == NULL) {
  					fprintf(stderr, USAGE, argv[0]);
					exit(EXIT_FAILURE);
				}

				*(s++) = '\0';
				gamma[1] = atof(g_s); /* Blue */
				gamma[2] = atof(s); /* Green */
			}
			break;
		case 'h':
			printf(HELP, argv[0]);
			exit(EXIT_SUCCESS);
			break;
		case 'l':
			s = strchr(optarg, ':');
			if (s == NULL) {
				fprintf(stderr, USAGE, argv[0]);
				exit(EXIT_FAILURE);
			}
			*(s++) = '\0';
			lat = atof(optarg);
			lon = atof(s);
			break;
		case 'm':
			if (strcmp(optarg, "randr") == 0 ||
			    strcmp(optarg, "RANDR") == 0) {
#ifdef ENABLE_RANDR
				use_randr = 1;
#else
				fprintf(stderr, "RANDR method was not"
					" enabled at compile time.\n");
				exit(EXIT_FAILURE);
#endif
			} else if (strcmp(optarg, "vidmode") == 0 ||
				   strcmp(optarg, "VidMode") == 0) {
#ifdef ENABLE_VIDMODE
				use_randr = 0;
#else
				fprintf(stderr, "VidMode method was not"
					" enabled at compile time.\n");
				exit(EXIT_FAILURE);
#endif
			} else {
				fprintf(stderr, "Unknown method `%s'.\n",
					optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case 'o':
			one_shot = 1;
			break;
		case 'r':
			init_trans = 0;
			break;
		case 's':
			screen_num = atoi(optarg);
			break;
		case 't':
			s = strchr(optarg, ':');
			if (s == NULL) {
				fprintf(stderr, USAGE, argv[0]);
				exit(EXIT_FAILURE);
			}
			*(s++) = '\0';
			temp_day = atoi(optarg);
			temp_night = atoi(s);
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			fprintf(stderr, USAGE, argv[0]);
			exit(EXIT_FAILURE);
			break;
		}
	}

	/* Latitude and longitude must be set */
	if (isnan(lat) || isnan(lon)) {
		fprintf(stderr, USAGE, argv[0]);
		fprintf(stderr, "Latitude and longitude must be set.\n");
		exit(EXIT_FAILURE);
	}

	if (verbose) {
		printf("Location: %f%lc, %f%lc\n",
		       lat, DEG_CHAR, lon, DEG_CHAR);
	}

	/* Latitude */
	if (lat < MIN_LAT || lat > MAX_LAT) {
		fprintf(stderr,
			"Latitude must be between %.1f%lc and %.1f%lc.\n",
			MIN_LAT, DEG_CHAR, MAX_LAT, DEG_CHAR);
		exit(EXIT_FAILURE);
	}

	/* Longitude */
	if (lon < MIN_LON || lon > MAX_LON) {
		fprintf(stderr,
			"Longitude must be between %.1f%lc and %.1f%lc.\n",
			MIN_LON, DEG_CHAR, MAX_LON, DEG_CHAR);
		exit(EXIT_FAILURE);
	}

	/* Color temperature at daytime */
	if (temp_day < MIN_TEMP || temp_day >= MAX_TEMP) {
		fprintf(stderr, "Temperature must be between %uK and %uK.\n",
			MIN_TEMP, MAX_TEMP);
		exit(EXIT_FAILURE);
	}

	/* Color temperature at night */
	if (temp_night < MIN_TEMP || temp_night >= MAX_TEMP) {
		fprintf(stderr, "Temperature must be between %uK and %uK.\n",
			MIN_TEMP, MAX_TEMP);
		exit(EXIT_FAILURE);
	}

	/* Gamma */
	if (gamma[0] < MIN_GAMMA || gamma[0] > MAX_GAMMA ||
	    gamma[1] < MIN_GAMMA || gamma[1] > MAX_GAMMA ||
	    gamma[2] < MIN_GAMMA || gamma[2] > MAX_GAMMA) {
		fprintf(stderr, "Gamma value must be between %.1f and %.1f.\n",
			MIN_GAMMA, MAX_GAMMA);
		exit(EXIT_FAILURE);
	}

	if (verbose) {
		printf("Gamma: %.3f, %.3f, %.3f\n",
		       gamma[0], gamma[1], gamma[2]);
	}

	if (one_shot) {
		/* Current angular elevation of the sun */
		struct timespec now;
		r = clock_gettime(CLOCK_REALTIME, &now);
		if (r < 0) {
			perror("clock_gettime");
			exit(EXIT_FAILURE);
		}

		double elevation = solar_elevation(now, lat, lon);

		if (verbose) {
			printf("Solar elevation: %f%lc\n", elevation,
			       DEG_CHAR);
		}

		/* Use elevation of sun to set color temperature */
		int temp = calculate_temp(elevation, temp_day, temp_night,
					  verbose);

		if (verbose) printf("Color temperature: %uK\n", temp);

		/* Adjust temperature */
		r = adjust_temperature(screen_num, use_randr, temp, gamma);
		if (r < 0) {
			fprintf(stderr, "Temperature adjustment failed.\n");
			exit(EXIT_FAILURE);
		}
	} else {
		/* Make a 10 second initial transition */
		struct timespec short_trans_end;
		r = clock_gettime(CLOCK_REALTIME, &short_trans_end);
		if (r < 0) {
			perror("clock_gettime");
			exit(EXIT_FAILURE);
		}

		int short_trans_len = 10;
		short_trans_end.tv_sec += short_trans_len;

		while (1) {
			/* Current angular elevation of the sun */
			struct timespec now;
			r = clock_gettime(CLOCK_REALTIME, &now);
			if (r < 0) {
				perror("clock_gettime");
				exit(EXIT_FAILURE);
			}

			double elevation = solar_elevation(now, lat, lon);

			/* Use elevation of sun to set color temperature */
			int temp = calculate_temp(elevation, temp_day,
						  temp_night, verbose);

			if (init_trans) {
				double start = now.tv_sec +
					now.tv_nsec / 1000000000.0;
				double end = short_trans_end.tv_sec +
					short_trans_end.tv_nsec /
					1000000000.0;
				double alpha = (end - start) /
					(float)short_trans_len;
				if (alpha < 0) init_trans = 0; /* Done */
				else temp = alpha*6500 + (1.0-alpha)*temp;
			}

			if (verbose) {
				printf("Temperature: %iK\n", temp);
			}

			/* Adjust temperature */
			r = adjust_temperature(screen_num, use_randr,
					       temp, gamma);
			if (r < 0) {
				fprintf(stderr, "Temperature adjustment"
					" failed.\n");
				exit(EXIT_FAILURE);
			}

			if (init_trans) usleep(100000);
			else usleep(5000000);
		}
	}

	return EXIT_SUCCESS;
}
