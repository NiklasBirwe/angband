/*
 * File: target.c
 * Purpose: Targetting code
 *
 * Copyright (c) 1997-2007 Angband contributors
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */

#include "angband.h"
#include "cave.h"
#include "cmd-core.h"
#include "keymap.h"
#include "mon-lore.h"
#include "mon-util.h"
#include "monster.h"
#include "obj-desc.h"
#include "obj-ignore.h"
#include "obj-ui.h"
#include "obj-util.h"
#include "player-timed.h"
#include "project.h"
#include "tables.h"
#include "target.h"
#include "trap.h"
#include "ui-map.h"

/*
 * Height of the help screen; any higher than 4 will overlap the health
 * bar which we want to keep in targeting mode.
 */
#define HELP_HEIGHT 3

/*** File-wide variables ***/

/* Is the target set? */
static bool target_set;

/* Current monster being tracked, or 0 */
static struct monster *target_who;

/* Target location */
static s16b target_x, target_y;

#define TS_INITIAL_SIZE	20

/*** Functions ***/

/*
 * Given a "source" and "target" location, extract a "direction",
 * which will move one step from the "source" towards the "target".
 *
 * Note that we use "diagonal" motion whenever possible.
 *
 * We return "5" if no motion is needed.
 *
 * XXX Change params to use two struct loc.
 */
int motion_dir(int y1, int x1, int y2, int x2)
{
	/* No movement required */
	if ((y1 == y2) && (x1 == x2)) return (DIR_NONE);

	/* South or North */
	if (x1 == x2) return ((y1 < y2) ? 2 : 8);

	/* East or West */
	if (y1 == y2) return ((x1 < x2) ? 6 : 4);

	/* South-east or South-west */
	if (y1 < y2) return ((x1 < x2) ? 3 : 1);

	/* North-east or North-west */
	if (y1 > y2) return ((x1 < x2) ? 9 : 7);

	/* Paranoia */
	return (5);
}


/*
 * Extract a direction (or zero) from a character
 */
int target_dir(struct keypress ch)
{
	return target_dir_allow(ch, FALSE);
}

int target_dir_allow(struct keypress ch, bool allow_5)
{
	int d = 0;

	/* Already a direction? */
	if (isdigit((unsigned char)ch.code)) {
		d = D2I(ch.code);
	} else if (isarrow(ch.code)) {
		switch (ch.code) {
			case ARROW_DOWN:  d = 2; break;
			case ARROW_LEFT:  d = 4; break;
			case ARROW_RIGHT: d = 6; break;
			case ARROW_UP:    d = 8; break;
		}
	} else {
		int mode;
		const struct keypress *act;

		if (OPT(rogue_like_commands))
			mode = KEYMAP_MODE_ROGUE;
		else
			mode = KEYMAP_MODE_ORIG;

		/* XXX see if this key has a digit in the keymap we can use */
		act = keymap_find(mode, ch);
		if (act) {
			const struct keypress *cur;
			for (cur = act; cur->type == EVT_KBRD; cur++) {
				if (isdigit((unsigned char) cur->code))
					d = D2I(cur->code);
			}
		}
	}

	/* Paranoia */
	if (d == 5 && !allow_5) d = 0;

	/* Return direction */
	return (d);
}

/*
 * Monster health description
 */
static void look_mon_desc(char *buf, size_t max, int m_idx)
{
	monster_type *m_ptr = cave_monster(cave, m_idx);

	bool living = TRUE;

	/* Determine if the monster is "living" (vs "undead") */
	if (monster_is_unusual(m_ptr->race)) living = FALSE;

	/* Healthy monsters */
	if (m_ptr->hp >= m_ptr->maxhp)
	{
		/* No damage */
		my_strcpy(buf, (living ? "unhurt" : "undamaged"), max);
	}
	else
	{
		/* Calculate a health "percentage" */
		int perc = 100L * m_ptr->hp / m_ptr->maxhp;

		if (perc >= 60)
			my_strcpy(buf, (living ? "somewhat wounded" : "somewhat damaged"), max);
		else if (perc >= 25)
			my_strcpy(buf, (living ? "wounded" : "damaged"), max);
		else if (perc >= 10)
			my_strcpy(buf, (living ? "badly wounded" : "badly damaged"), max);
		else
			my_strcpy(buf, (living ? "almost dead" : "almost destroyed"), max);
	}

	if (m_ptr->m_timed[MON_TMD_SLEEP]) my_strcat(buf, ", asleep", max);
	if (m_ptr->m_timed[MON_TMD_CONF]) my_strcat(buf, ", confused", max);
	if (m_ptr->m_timed[MON_TMD_FEAR]) my_strcat(buf, ", afraid", max);
	if (m_ptr->m_timed[MON_TMD_STUN]) my_strcat(buf, ", stunned", max);
}



/*
 * Determine is a monster makes a reasonable target
 *
 * The concept of "targetting" was stolen from "Morgul" (?)
 *
 * The player can target any location, or any "target-able" monster.
 *
 * Currently, a monster is "target_able" if it is visible, and if
 * the player can hit it with a projection, and the player is not
 * hallucinating.  This allows use of "use closest target" macros.
 */
bool target_able(struct monster *m)
{
	return m && m->race && m->ml && !m->unaware &&
		projectable(cave, player->py, player->px, m->fy, m->fx, PROJECT_NONE) &&
		!player->timed[TMD_IMAGE];
}



/*
 * Update (if necessary) and verify (if possible) the target.
 *
 * We return TRUE if the target is "okay" and FALSE otherwise.
 */
bool target_okay(void)
{
	/* No target */
	if (!target_set) return FALSE;

	/* Check "monster" targets */
	if (target_who) {
		if (target_able(target_who)) {
			/* Get the monster location */
			target_y = target_who->fy;
			target_x = target_who->fx;

			/* Good target */
			return TRUE;
		}
	} else if (target_x && target_y) {
		/* Allow a direction without a monster */
		return TRUE;
	}

	/* Assume no target */
	return FALSE;
}


/*
 * Set the target to a monster (or nobody)
 */
bool target_set_monster(struct monster *mon)
{
	/* Acceptable target */
	if (mon && target_able(mon))
	{
		target_set = TRUE;
		target_who = mon;
		target_y = mon->fy;
		target_x = mon->fx;
		return TRUE;
	}

	/* Reset target info */
	target_set = FALSE;
	target_who = NULL;
	target_y = 0;
	target_x = 0;

	return FALSE;
}


/*
 * Set the target to a location
 */
void target_set_location(int y, int x)
{
	/* Legal target */
	if (square_in_bounds_fully(cave, y, x))
	{
		/* Save target info */
		target_set = TRUE;
		target_who = NULL;
		target_y = y;
		target_x = x;
	}

	/* Clear target */
	else
	{
		/* Reset target info */
		target_set = FALSE;
		target_who = 0;
		target_y = 0;
		target_x = 0;
	}
}



/*
 * Sorting hook -- comp function -- by "distance to player"
 *
 * We use "u" and "v" to point to arrays of "x" and "y" positions,
 * and sort the arrays by double-distance to the player.
 */
static int cmp_distance(const void *a, const void *b)
{
	int py = player->py;
	int px = player->px;

	const struct loc *pa = a;
	const struct loc *pb = b;

	int da, db, kx, ky;

	/* Absolute distance components */
	kx = pa->x; kx -= px; kx = ABS(kx);
	ky = pa->y; ky -= py; ky = ABS(ky);

	/* Approximate Double Distance to the first point */
	da = ((kx > ky) ? (kx + kx + ky) : (ky + ky + kx));

	/* Absolute distance components */
	kx = pb->x; kx -= px; kx = ABS(kx);
	ky = pb->y; ky -= py; ky = ABS(ky);

	/* Approximate Double Distance to the first point */
	db = ((kx > ky) ? (kx + kx + ky) : (ky + ky + kx));

	/* Compare the distances */
	if (da < db)
		return -1;
	if (da > db)
		return 1;
	return 0;
}

/*
 * Hack -- help "select" a location (see below)
 */
static s16b target_pick(int y1, int x1, int dy, int dx, struct point_set *targets)
{
	int i, v;

	int x2, y2, x3, y3, x4, y4;

	int b_i = -1, b_v = 9999;


	/* Scan the locations */
	for (i = 0; i < point_set_size(targets); i++)
	{
		/* Point 2 */
		x2 = targets->pts[i].x;
		y2 = targets->pts[i].y;

		/* Directed distance */
		x3 = (x2 - x1);
		y3 = (y2 - y1);

		/* Verify quadrant */
		if (dx && (x3 * dx <= 0)) continue;
		if (dy && (y3 * dy <= 0)) continue;

		/* Absolute distance */
		x4 = ABS(x3);
		y4 = ABS(y3);

		/* Verify quadrant */
		if (dy && !dx && (x4 > y4)) continue;
		if (dx && !dy && (y4 > x4)) continue;

		/* Approximate Double Distance */
		v = ((x4 > y4) ? (x4 + x4 + y4) : (y4 + y4 + x4));

		/* Penalize location XXX XXX XXX */

		/* Track best */
		if ((b_i >= 0) && (v >= b_v)) continue;

		/* Track best */
		b_i = i; b_v = v;
	}

	/* Result */
	return (b_i);
}


/*
 * Hack -- determine if a given location is "interesting"
 */
static bool target_set_interactive_accept(int y, int x)
{
	object_type *o_ptr;


	/* Player grids are always interesting */
	if (cave->m_idx[y][x] < 0) return (TRUE);


	/* Handle hallucination */
	if (player->timed[TMD_IMAGE]) return (FALSE);


	/* Visible monsters */
	if (cave->m_idx[y][x] > 0)
	{
		monster_type *m_ptr = square_monster(cave, y, x);

		/* Visible monsters */
		if (m_ptr->ml && !m_ptr->unaware) return (TRUE);
	}

    /* Traps */
    if (square_visible_trap(cave, y, x))
		return(TRUE);

	/* Scan all objects in the grid */
	for (o_ptr = get_first_object(y, x); o_ptr; o_ptr = get_next_object(o_ptr))
	{
		/* Memorized object */
		if (o_ptr->marked && !ignore_item_ok(o_ptr)) return (TRUE);
	}

	/* Interesting memorized features */
	if (square_ismark(cave, y, x) && !square_isboring(cave, y, x))
		return (TRUE);

	/* Nope */
	return (FALSE);
}

/*
 * Return a target set of target_able monsters.
 */
static struct point_set *target_set_interactive_prepare(int mode)
{
	int y, x;
	struct point_set *targets = point_set_new(TS_INITIAL_SIZE);

	/* Scan the current panel */
	for (y = Term->offset_y; y < Term->offset_y + SCREEN_HGT; y++)
	{
		for (x = Term->offset_x; x < Term->offset_x + SCREEN_WID; x++)
		{
			/* Check bounds */
			if (!square_in_bounds_fully(cave, y, x)) continue;

			/* Require "interesting" contents */
			if (!target_set_interactive_accept(y, x)) continue;

			/* Special mode */
			if (mode & (TARGET_KILL))
			{
				/* Must contain a monster */
				if (!(cave->m_idx[y][x] > 0)) continue;

				/* Must be a targettable monster */
			 	if (!target_able(square_monster(cave, y, x))) continue;
			}

			/* Save the location */
			add_to_point_set(targets, y, x);
		}
	}

	sort(targets->pts, point_set_size(targets), sizeof(*(targets->pts)), cmp_distance);
	return targets;
}

/*
 * Perform the minimum "whole panel" adjustment to ensure that the given
 * location is contained inside the current panel, and return TRUE if any
 * such adjustment was performed. Optionally accounts for the targeting
 * help window.
 */
static bool adjust_panel_help(int y, int x, bool help)
{
	bool changed = FALSE;

	int j;

	int screen_hgt_main = help ? (Term->hgt - ROW_MAP - 3) 
			 : (Term->hgt - ROW_MAP - 1);

	/* Scan windows */
	for (j = 0; j < ANGBAND_TERM_MAX; j++)
	{
		int wx, wy;
		int screen_hgt, screen_wid;

		term *t = angband_term[j];

		/* No window */
		if (!t) continue;

		/* No relevant flags */
		if ((j > 0) && !(window_flag[j] & PW_MAP)) continue;

		wy = t->offset_y;
		wx = t->offset_x;

		screen_hgt = (j == 0) ? screen_hgt_main : t->hgt;
		screen_wid = (j == 0) ? (Term->wid - COL_MAP - 1) : t->wid;

		/* Bigtile panels need adjustment */
		screen_wid = screen_wid / tile_width;
		screen_hgt = screen_hgt / tile_height;

		/* Adjust as needed */
		while (y >= wy + screen_hgt) wy += screen_hgt / 2;
		while (y < wy) wy -= screen_hgt / 2;

		/* Adjust as needed */
		while (x >= wx + screen_wid) wx += screen_wid / 2;
		while (x < wx) wx -= screen_wid / 2;

		/* Use "modify_panel" */
		if (modify_panel(t, wy, wx)) changed = TRUE;
	}

	return (changed);
}


/*
 * Describe a location relative to the player position.
 * e.g. "12 S 35 W" or "0 N, 33 E" or "0 N, 0 E"
 */
static void coords_desc(char *buf, int size, int y, int x)
{
	const char *east_or_west;
	const char *north_or_south;

	int py = player->py;
	int px = player->px;

	if (y > py)
		north_or_south = "S";
	else
		north_or_south = "N";

	if (x < px)
		east_or_west = "W";
	else
		east_or_west = "E";

	strnfmt(buf, size, "%d %s, %d %s",
		ABS(y-py), north_or_south, ABS(x-px), east_or_west);
}

/*
 * Display targeting help at the bottom of the screen.
 */
static void target_display_help(bool monster, bool free)
{
	/* Determine help location */
	int wid, hgt, help_loc;
	Term_get_size(&wid, &hgt);
	help_loc = hgt - HELP_HEIGHT;
	
	/* Clear */
	clear_from(help_loc);

	/* Prepare help hooks */
	text_out_hook = text_out_to_screen;
	text_out_indent = 1;
	Term_gotoxy(1, help_loc);

	/* Display help */
	text_out_c(TERM_L_GREEN, "<dir>");
	text_out(" and ");
	text_out_c(TERM_L_GREEN, "<click>");
	text_out(" look around. '");
	text_out_c(TERM_L_GREEN, "g");
	text_out(" moves to the selection. '");
	text_out_c(TERM_L_GREEN, "p");
	text_out("' selects the player. '");
	text_out_c(TERM_L_GREEN, "q");
	text_out("' exits. '");
	text_out_c(TERM_L_GREEN, "r");
	text_out("' displays details. '");

	if (free)
	{
		text_out_c(TERM_L_GREEN, "m");
		text_out("' restricts to interesting places. ");
	}
	else
	{
		text_out_c(TERM_L_GREEN, "+");
		text_out("' and '");
		text_out_c(TERM_L_GREEN, "-");
		text_out("' cycle through interesting places. '");
		text_out_c(TERM_L_GREEN, "o");
		text_out("' allows free selection. ");
	}
	
	if (monster || free)
	{
		text_out("'");
		text_out_c(TERM_L_GREEN, "t");
		text_out("' targets the current selection.");
	}

	/* Reset */
	text_out_indent = 0;
}

/* Size of the array that is used for object names during targeting. */
#define TARGET_OUT_VAL_SIZE 256

/**
 * Display the object name of the selected object and allow for full object recall. Returns
 * an event that occurred display.
 *
 * This will only work for a single object on the ground and not a pile. This loop is
 * similar to the monster recall loop in target_set_interactive_aux(). The out_val array
 * size needs to match the size that is passed in (since this code was extracted from there).
 *
 * \param o_ptr is the object to describe.
 * \param y is the cave row of the object.
 * \param x is the cave column of the object.
 * \param out_val is the string that holds the name of the object and is returned to the caller.
 * \param s1 is part of the output string.
 * \param s2 is part of the output string.
 * \param s3 is part of the output string.
 * \param coords is part of the output string
 */
static ui_event target_recall_loop_object(object_type *o_ptr, int y, int x, char out_val[TARGET_OUT_VAL_SIZE], const char *s1, const char *s2, const char *s3, char *coords)
{
	bool recall = FALSE;
	ui_event press;

	while (1) {
		if (recall) {
			display_object_recall_interactive(o_ptr);
			press = inkey_m();
		}
		else {
			char o_name[80];

			/* Obtain an object description */
			object_desc(o_name, sizeof(o_name), o_ptr,
						ODESC_PREFIX | ODESC_FULL);

			/* Describe the object */
			if (player->wizard)
			{
				strnfmt(out_val, TARGET_OUT_VAL_SIZE,
						"%s%s%s%s, %s (%d:%d, cost=%d, when=%d).",
						s1, s2, s3, o_name, coords, y, x, (int)cave->cost[y][x], (int)cave->when[y][x]);
			}
			else
			{
				strnfmt(out_val, TARGET_OUT_VAL_SIZE,
						"%s%s%s%s, %s.", s1, s2, s3, o_name, coords);
			}

			prt(out_val, 0, 0);
			move_cursor_relative(y, x);
			press = inkey_m();
		}

		if ((press.type == EVT_MOUSE) && (press.mouse.button == 1) && (KEY_GRID_X(press) == x) && (KEY_GRID_Y(press) == y))
			recall = !recall;
		else if ((press.type == EVT_KBRD) && (press.key.code == 'r'))
			recall = !recall;
		else
			break;
	}

	return press;
}

/*
 * Examine a grid, return a keypress.
 *
 * The "mode" argument contains the "TARGET_LOOK" bit flag, which
 * indicates that the "space" key should scan through the contents
 * of the grid, instead of simply returning immediately.  This lets
 * the "look" command get complete information, without making the
 * "target" command annoying.
 *
 * The "info" argument contains the "commands" which should be shown
 * inside the "[xxx]" text.  This string must never be empty, or grids
 * containing monsters will be displayed with an extra comma.
 *
 * Note that if a monster is in the grid, we update both the monster
 * recall info and the health bar info to track that monster.
 *
 * This function correctly handles multiple objects per grid, and objects
 * and terrain features in the same grid, though the latter never happens.
 *
 * This function must handle blindness/hallucination.
 */
//static struct keypress target_set_interactive_aux(int y, int x, int mode)
static ui_event target_set_interactive_aux(int y, int x, int mode)
{
	s16b this_o_idx = 0, next_o_idx = 0;

	const char *s1, *s2, *s3;

	bool boring;

	int floor_list[MAX_FLOOR_STACK];
	int floor_num;

	//struct keypress query;
	ui_event press;

	char out_val[TARGET_OUT_VAL_SIZE];

	char coords[20];

	const char *name;

	/* Describe the square location */
	coords_desc(coords, sizeof(coords), y, x);

	/* Repeat forever */
	while (1)
	{
		/* Paranoia */
		press.type = EVT_KBRD;
		press.key.code = ' ';
		press.key.mods = 0;

		/* Assume boring */
		boring = TRUE;

		/* Default */
		s1 = "You see ";
		s2 = "";
		s3 = "";


		/* The player */
		if (cave->m_idx[y][x] < 0)
		{
			/* Description */
			s1 = "You are ";

			/* Preposition */
			s2 = "on ";
		}

		/* Hallucination messes things up */
		if (player->timed[TMD_IMAGE])
		{
			const char *name = "something strange";

			/* Display a message */
			if (player->wizard)
				strnfmt(out_val, sizeof(out_val), "%s%s%s%s, %s (%d:%d, cost=%d, when=%d).",
						s1, s2, s3, name, coords, y, x, (int)cave->cost[y][x], (int)cave->when[y][x]);
			else
				strnfmt(out_val, sizeof(out_val), "%s%s%s%s, %s.",
						s1, s2, s3, name, coords);

			prt(out_val, 0, 0);
			move_cursor_relative(y, x);
			//input = inkey_m();
			//if (
			press.key = inkey();

			/* Stop on everything but "return" */
			if (press.key.code == KC_ENTER)
				continue;

			return press;
		}

		/* Actual monsters */
		if (cave->m_idx[y][x] > 0)
		{
			monster_type *m_ptr = square_monster(cave, y, x);
			const monster_lore *l_ptr = get_lore(m_ptr->race);

			/* Visible */
			if (m_ptr->ml && !m_ptr->unaware)
			{
				bool recall = FALSE;

				char m_name[80];

				/* Not boring */
				boring = FALSE;

				/* Get the monster name ("a kobold") */
				monster_desc(m_name, sizeof(m_name), m_ptr, MDESC_IND_VIS);

				/* Hack -- track this monster race */
				monster_race_track(player->upkeep, m_ptr->race);

				/* Hack -- health bar for this monster */
				health_track(player->upkeep, m_ptr);

				/* Hack -- handle stuff */
				handle_stuff(player->upkeep);

				/* Interact */
				while (1)
				{
					/* Recall */
					if (recall)
					{
						lore_show_interactive(m_ptr->race, l_ptr);
						press = inkey_m();
					}

					/* Normal */
					else
					{
						char buf[80];

						/* Describe the monster */
						look_mon_desc(buf, sizeof(buf), cave->m_idx[y][x]);

						/* Describe, and prompt for recall */
						if (player->wizard)
						{
							strnfmt(out_val, sizeof(out_val),
									"%s%s%s%s (%s), %s (%d:%d, cost=%d, when=%d).",
									s1, s2, s3, m_name, buf, coords, y, x, (int)cave->cost[y][x], (int)cave->when[y][x]);
						}
						else
						{
							strnfmt(out_val, sizeof(out_val),
									"%s%s%s%s (%s), %s.",
									s1, s2, s3, m_name, buf, coords);
						}

						prt(out_val, 0, 0);

						/* Place cursor */
						move_cursor_relative(y, x);

						/* Command */
						press = inkey_m();
					}

					/* Normal commands */
				  if ((press.type == EVT_MOUSE) && (press.mouse.button == 1) && (KEY_GRID_X(press) == x) && (KEY_GRID_Y(press) == y))
						recall = !recall;
					else
					if ((press.type == EVT_KBRD) && (press.key.code == 'r'))
						recall = !recall;
					else
						break;
				}

				if (press.type == EVT_MOUSE) {
					/* Stop on right click */
					if (press.mouse.button == 2)
						break;

					/* Sometimes stop at "space" key */
					if (press.mouse.button && !(mode & (TARGET_LOOK))) break;
				} else {
					/* Stop on everything but "return"/"space" */
					if (press.key.code != KC_ENTER && press.key.code != ' ')
						break;

					/* Sometimes stop at "space" key */
					if ((press.key.code == ' ') && !(mode & (TARGET_LOOK))) break;
				}

				/* Take account of gender */
				if (rf_has(m_ptr->race->flags, RF_FEMALE)) s1 = "She is ";
				else if (rf_has(m_ptr->race->flags, RF_MALE)) s1 = "He is ";
				else s1 = "It is ";

				/* Use a verb */
				s2 = "carrying ";

				/* Scan all objects being carried */
				for (this_o_idx = m_ptr->hold_o_idx; this_o_idx; this_o_idx = next_o_idx)
				{
					char o_name[80];

					object_type *o_ptr;

					/* Get the object */
					o_ptr = cave_object(cave, this_o_idx);

					/* Get the next object */
					next_o_idx = o_ptr->next_o_idx;

					/* Obtain an object description */
					object_desc(o_name, sizeof(o_name), o_ptr,
								ODESC_PREFIX | ODESC_FULL);

					/* Describe the object */
					if (player->wizard)
					{
						strnfmt(out_val, sizeof(out_val),
								"%s%s%s%s, %s (%d:%d, cost=%d, when=%d).",
								s1, s2, s3, o_name, coords, y, x, (int)cave->cost[y][x], (int)cave->when[y][x]);
					}
					/* Disabled since monsters now carry their drops
					else
					{
						strnfmt(out_val, sizeof(out_val),
								"%s%s%s%s, %s.", s1, s2, s3, o_name, coords);
					} */

					prt(out_val, 0, 0);
					move_cursor_relative(y, x);
					press = inkey_m();

					if (press.type == EVT_MOUSE) {
						/* Stop on right click */
						if (press.mouse.button == 2)
							break;

						/* Sometimes stop at "space" key */
						if (press.mouse.button && !(mode & (TARGET_LOOK))) break;
					} else {
						/* Stop on everything but "return"/"space" */
						if ((press.key.code != KC_ENTER) && (press.key.code != ' ')) break;

						/* Sometimes stop at "space" key */
						if ((press.key.code == ' ') && !(mode & (TARGET_LOOK))) break;
					}

					/* Change the intro */
					s2 = "also carrying ";
				}

				/* Double break */
				if (this_o_idx) break;

				/* Use a preposition */
				s2 = "on ";
			}
		}

		/* A trap */
		if (square_visible_trap(cave, y, x)) 
		{
			struct trap *trap = cave_trap(cave, square_trap_idx(cave, y, x));

			/* Not boring */
			boring = FALSE;

			/* Interact */
			while (1) 
			{
				/* Change the intro */
				if (cave->m_idx[y][x] < 0) 
				{
					s1 = "You are ";
					s2 = "on ";
				}
				else
				{
					s1 = "You see ";
					s2 = "";
				}

				/* Pick proper indefinite article */
				s3 = (is_a_vowel(trap->kind->name[0])) ? "an " : "a ";

				/* Describe, and prompt for recall */
				if (player->wizard) 
				{
					strnfmt(out_val, sizeof(out_val),
							"%s%s%s%s, %s (%d:%d, cost=%d, when=%d).", s1, s2, s3,
							trap->kind->name, coords, y, x, (int)cave->cost[y][x], (int)cave->when[y][x]);
				} 
				else 
				{
					strnfmt(out_val, sizeof(out_val), "%s%s%s%s, %s.", 
							s1, s2, s3, trap->kind->name, coords);
				}

				prt(out_val, 0, 0);

				/* Place cursor */
				move_cursor_relative(y, x);

				/* Command */
				press = inkey_m();
		
				/* Stop on everything but "return"/"space" */
				if ((press.key.code != KC_ENTER) && (press.key.code != ' '))
					break;
		
				/* Sometimes stop at "space" key */
				if ((press.key.code == ' ') && !(mode & (TARGET_LOOK)))
					break;
			}
		}
	
		/* Double break */
		if (square_visible_trap(cave, y, x))
			break;
	
		/* Assume not floored */
		floor_num = scan_floor(floor_list, N_ELEMENTS(floor_list), y, x, 0x0A, NULL);

		/* Scan all marked objects in the grid */
		if ((floor_num > 0) &&
		    (!(player->timed[TMD_BLIND]) || (y == player->py && x == player->px)))
		{
			/* Not boring */
			boring = FALSE;

			track_object(player->upkeep, -floor_list[0]);
			handle_stuff(player->upkeep);

			/* If there is more than one item... */
			if (floor_num > 1) while (1)
			{
				/* Describe the pile */
				if (player->wizard)
				{
					strnfmt(out_val, sizeof(out_val),
							"%s%s%sa pile of %d objects, %s (%d:%d, cost=%d, when=%d).",
							s1, s2, s3, floor_num, coords, y, x, (int)cave->cost[y][x], (int)cave->when[y][x]);
				}
				else
				{
					strnfmt(out_val, sizeof(out_val),
							"%s%s%sa pile of %d objects, %s.",
							s1, s2, s3, floor_num, coords);
				}

				prt(out_val, 0, 0);
				move_cursor_relative(y, x);
				press = inkey_m();

				/* Display objects */
				if (((press.type == EVT_MOUSE) && (press.mouse.button == 1) && (KEY_GRID_X(press) == x) && (KEY_GRID_Y(press) == y))
						|| ((press.type == EVT_KBRD) && (press.key.code == 'r')))
				{
					int rdone = 0;
					int pos;
					while (!rdone)
					{
						/* Save screen */
						screen_save();

						/* Display */
						show_floor(floor_list, floor_num, (OLIST_WEIGHT | OLIST_GOLD), NULL);

						/* Describe the pile */
						prt(out_val, 0, 0);
						press = inkey_m();

						/* Load screen */
						screen_load();

						if (press.type == EVT_MOUSE) {
							pos = press.mouse.y-1;
						} else {
							pos = press.key.code - 'a';
						}
						if (0 <= pos && pos < floor_num)
						{
							track_object(player->upkeep, -floor_list[pos]);
							handle_stuff(player->upkeep);
							continue;
						}
						rdone = 1;
					}

					/* Now that the user's done with the display loop, let's */
					/* the outer loop over again */
					continue;
				}

				/* Done */
				break;
			}
			/* Only one object to display */
			else
			{
				/* Get the single object in the list */
				object_type *o_ptr = cave_object(cave, floor_list[0]);

				/* Allow user to recall an object */
				press = target_recall_loop_object(o_ptr, y, x, out_val, s1, s2, s3, coords);

				/* Stop on everything but "return"/"space" */
				if ((press.key.code != KC_ENTER) && (press.key.code != ' ')) break;

				/* Sometimes stop at "space" key */
				if ((press.key.code == ' ') && !(mode & (TARGET_LOOK))) break;

				/* Plurals */
				s1 = VERB_AGREEMENT(o_ptr->number, "It is ", "They are ");

				/* Preposition */
				s2 = "on ";
			}

		}

		/* Double break */
		if (this_o_idx) break;

		name = square_apparent_name(cave, player, y, x);

		/* Terrain feature if needed */
		if (boring || square_isinteresting(cave, y, x))
		{
			/* Hack -- handle unknown grids */

			/* Pick a prefix */
			if (*s2 && square_isdoor(cave, y, x)) s2 = "in ";

			/* Pick proper indefinite article */
			s3 = (is_a_vowel(name[0])) ? "an " : "a ";

			/* Hack -- special introduction for store doors */
			if (square_isshop(cave, y, x))
			{
				s3 = "the entrance to the ";
			}

			/* Display a message */
			if (player->wizard)
			{
				strnfmt(out_val, sizeof(out_val),
						"%s%s%s%s, %s (%d:%d, cost=%d, when=%d).", s1, s2, s3, name, coords, y, x, (int)cave->cost[y][x], (int)cave->when[y][x]);
			}
			else
			{
				strnfmt(out_val, sizeof(out_val),
						"%s%s%s%s, %s.", s1, s2, s3, name, coords);
			}

			prt(out_val, 0, 0);
			move_cursor_relative(y, x);
			press = inkey_m();

			if (press.type == EVT_MOUSE) {
				/* Stop on right click */
				if (press.mouse.button == 2)
					break;
			} else {
				/* Stop on everything but "return"/"space" */
				if ((press.key.code != KC_ENTER) && (press.key.code != ' ')) break;
			}
		}

		/* Stop on everything but "return" */
		if (press.type == EVT_MOUSE) {
				/* Stop on right click */
				if (press.mouse.button != 2)
					break;
		} else {
    			if (press.key.code != KC_ENTER) break;
		}
	}

	/* Keep going */
	return (press);
}


bool target_set_closest(int mode)
{
	int y, x;
	monster_type *m_ptr;
	char m_name[80];
	bool visibility;
	struct point_set *targets;

	/* Cancel old target */
	target_set_monster(0);

	/* Get ready to do targetting */
	targets = target_set_interactive_prepare(mode);

	/* If nothing was prepared, then return */
	if (point_set_size(targets) < 1)
	{
		msg("No Available Target.");
		point_set_dispose(targets);
		return FALSE;
	}

	/* Find the first monster in the queue */
	y = targets->pts[0].y;
	x = targets->pts[0].x;
	m_ptr = square_monster(cave, y, x);
	
	/* Target the monster, if possible */
	if (!target_able(m_ptr))
	{
		msg("No Available Target.");
		point_set_dispose(targets);
		return FALSE;
	}

	/* Target the monster */
	monster_desc(m_name, sizeof(m_name), m_ptr, MDESC_CAPITAL);
	if (!(mode & TARGET_QUIET))
		msg("%s is targeted.", m_name);
	Term_fresh();

	/* Set up target information */
	monster_race_track(player->upkeep, m_ptr->race);
	health_track(player->upkeep, m_ptr);
	target_set_monster(m_ptr);

	/* Visual cue */
	Term_get_cursor(&visibility);
	(void)Term_set_cursor(TRUE);
	move_cursor_relative(y, x);
	Term_redraw_section(x, y, x, y);

	/* TODO: what's an appropriate amount of time to spend highlighting */
	Term_xtra(TERM_XTRA_DELAY, 150);
	(void)Term_set_cursor(visibility);

	point_set_dispose(targets);
	return TRUE;
}


/**
 * Draw a visible path over the squares between (x1,y1) and (x2,y2).
 *
 * The path consists of "*", which are white except where there is a
 * monster, object or feature in the grid.
 *
 * This routine has (at least) three weaknesses:
 * - remembered objects/walls which are no longer present are not shown,
 * - squares which (e.g.) the player has walked through in the dark are
 *   treated as unknown space.
 * - walls which appear strange due to hallucination aren't treated correctly.
 *
 * The first two result from information being lost from the dungeon arrays,
 * which requires changes elsewhere
 */
static int draw_path(u16b path_n, struct loc *path_g, wchar_t *c, int *a, int y1, int x1)
{
	int i;
	bool on_screen;

	/* No path, so do nothing. */
	if (path_n < 1) return 0;

	/* The starting square is never drawn, but notice if it is being
     * displayed. In theory, it could be the last such square.
     */
	on_screen = panel_contains(y1, x1);

	/* Draw the path. */
	for (i = 0; i < path_n; i++) {
		byte colour;

		/* Find the co-ordinates on the level. */
		int y = path_g[i].y;
		int x = path_g[i].x;

		/*
		 * As path[] is a straight line and the screen is oblong,
		 * there is only section of path[] on-screen.
		 * If the square being drawn is visible, this is part of it.
		 * If none of it has been drawn, continue until some of it
		 * is found or the last square is reached.
		 * If some of it has been drawn, finish now as there are no
		 * more visible squares to draw.
		 */
		 if (panel_contains(y,x)) on_screen = TRUE;
		 else if (on_screen) break;
		 else continue;

	 	/* Find the position on-screen */
		move_cursor_relative(y,x);

		/* This square is being overwritten, so save the original. */
		Term_what(Term->scr->cx, Term->scr->cy, a+i, c+i);

		/* Choose a colour. */
		if (cave->m_idx[y][x] && square_monster(cave, y, x)->ml) {
			/* Visible monsters are red. */
			monster_type *m_ptr = square_monster(cave, y, x);

			/* Mimics act as objects */
			if (rf_has(m_ptr->race->flags, RF_UNAWARE)) 
				colour = TERM_YELLOW;
			else
				colour = TERM_L_RED;
		}

		else if (cave->o_idx[y][x] && square_object(cave, y, x)->marked)
			/* Known objects are yellow. */
			colour = TERM_YELLOW;

		else if ((!square_isprojectable(cave, y,x) &&
				  square_ismark(cave, y, x)) || player_can_see_bold(y,x))
			/* Known walls are blue. */
			colour = TERM_BLUE;

		else if (!square_ismark(cave, y, x) && !player_can_see_bold(y,x))
			/* Unknown squares are grey. */
			colour = TERM_L_DARK;

		else
			/* Unoccupied squares are white. */
			colour = TERM_WHITE;

		/* Draw the path segment */
		(void)Term_addch(colour, L'*');
	}
	return i;
}


/**
 * Load the attr/char at each point along "path" which is on screen from
 * "a" and "c". This was saved in draw_path().
 */
static void load_path(u16b path_n, struct loc *path_g, wchar_t *c, int *a)
{
	int i;
	for (i = 0; i < path_n; i++) {
		int y = path_g[i].y;
		int x = path_g[i].x;

		if (!panel_contains(y, x)) continue;
		move_cursor_relative(y, x);
		Term_addch(a[i], c[i]);
	}

	Term_fresh();
}


/*
 * Handle "target" and "look".
 *
 * Note that this code can be called from "get_aim_dir()".
 *
 * Currently, when "flag" is true, that is, when
 * "interesting" grids are being used, and a directional key is used, we
 * only scroll by a single panel, in the direction requested, and check
 * for any interesting grids on that panel.  The "correct" solution would
 * actually involve scanning a larger set of grids, including ones in
 * panels which are adjacent to the one currently scanned, but this is
 * overkill for this function.  XXX XXX
 *
 * Hack -- targetting/observing an "outer border grid" may induce
 * problems, so this is not currently allowed.
 *
 * The player can use the direction keys to move among "interesting"
 * grids in a heuristic manner, or the "space", "+", and "-" keys to
 * move through the "interesting" grids in a sequential manner, or
 * can enter "location" mode, and use the direction keys to move one
 * grid at a time in any direction.  The "t" (set target) command will
 * only target a monster (as opposed to a location) if the monster is
 * target_able and the "interesting" mode is being used.
 *
 * The current grid is described using the "look" method above, and
 * a new command may be entered at any time, but note that if the
 * "TARGET_LOOK" bit flag is set (or if we are in "location" mode,
 * where "space" has no obvious meaning) then "space" will scan
 * through the description of the current grid until done, instead
 * of immediately jumping to the next "interesting" grid.  This
 * allows the "target" command to retain its old semantics.
 *
 * The "*", "+", and "-" keys may always be used to jump immediately
 * to the next (or previous) interesting grid, in the proper mode.
 *
 * The "return" key may always be used to scan through a complete
 * grid description (forever).
 *
 * This command will cancel any old target, even if used from
 * inside the "look" command.
 *
 *
 * 'mode' is one of TARGET_LOOK or TARGET_KILL.
 * 'x' and 'y' are the initial position of the target to be highlighted,
 * or -1 if no location is specified.
 * Returns TRUE if a target has been successfully set, FALSE otherwise.
 */
bool target_set_interactive(int mode, int x, int y)
{
	int py = player->py;
	int px = player->px;

	int path_n;
	struct loc path_g[256];

	int i, d, m, t, bd;
	int wid, hgt, help_prompt_loc;

	bool done = FALSE;
	bool flag = TRUE;
	bool help = FALSE;

	ui_event press;

	/* These are used for displaying the path to the target */
	wchar_t path_char[MAX_RANGE_LGE];
	int path_attr[MAX_RANGE_LGE];
	struct point_set *targets;

	/* If we haven't been given an initial location, start on the
	   player. */
	if (x == -1 || y == -1)
	{
		x = player->px;
		y = player->py;
	}
	/* If we /have/ been given an initial location, make sure we
	   honour it by going into "free targetting" mode. */
	else
	{
		flag = FALSE;
	}

	/* Cancel target */
	target_set_monster(0);

	/* Calculate the window location for the help prompt */
	Term_get_size(&wid, &hgt);
	help_prompt_loc = hgt - 1;
	
	/* Display the help prompt */
	prt("Press '?' for help.", help_prompt_loc, 0);

	/* Prepare the target set */
	targets = target_set_interactive_prepare(mode);

	/* Start near the player */
	m = 0;

	/* Interact */
	while (!done) {
		bool path_drawn = FALSE;
		
		/* Interesting grids */
		if (flag && point_set_size(targets))
		{
			y = targets->pts[m].y;
			x = targets->pts[m].x;

			/* Adjust panel if needed */
			if (adjust_panel_help(y, x, help)) handle_stuff(player->upkeep);
		
			/* Update help */
			if (help) {
				bool good_target = target_able(square_monster(cave, y, x));
				target_display_help(good_target, !(flag && point_set_size(targets)));
			}

			/* Find the path. */
			path_n = project_path(path_g, MAX_RANGE, py, px, y, x, PROJECT_THRU);

			/* Draw the path in "target" mode. If there is one */
			if (mode & (TARGET_KILL))
				path_drawn = draw_path(path_n, path_g, path_char, path_attr, py, px);

			/* Describe and Prompt */
			press = target_set_interactive_aux(y, x, mode);

			/* Remove the path */
			if (path_drawn) load_path(path_n, path_g, path_char, path_attr);

			/* Assume no "direction" */
			d = 0;


			/* Analyze */
			if (press.type == EVT_MOUSE) {
				if (press.mouse.button == 3) {
					/* give the target selection command */
					press.mouse.button = 2;
					press.mouse.mods = KC_MOD_CONTROL;
				}
				if (press.mouse.button == 2) {
					y = KEY_GRID_Y(press);//.mouse.y;
					x = KEY_GRID_X(press);//.mouse.x;
					if (press.mouse.mods & KC_MOD_CONTROL) {
						/* same as keyboard target selection command below */
						struct monster *m = square_monster(cave, y, x);

						if (target_able(m)) {
							/* Set up target information */
							monster_race_track(player->upkeep, m->race);
							health_track(player->upkeep, m);
							target_set_monster(m);
							done = TRUE;
						} else {
							bell("Illegal target!");
						}
					} else
					if (press.mouse.mods & KC_MOD_ALT) {
						/* go to spot - same as 'g' command below */
						cmdq_push(CMD_PATHFIND);
						cmd_set_arg_point(cmdq_peek(), "point", y, x);
						done = TRUE;
					} else
					{
						/* cancel look mode */
						done = TRUE;
					}
				} else
				/*if (press.mouse.button == 3) {
				} else*/
				{
					y = KEY_GRID_Y(press);//.mouse.y;
					x = KEY_GRID_X(press);//.mouse.x;
					if (cave->m_idx[y][x] || cave->o_idx[y][x]){// || cave->feat[y][x]&) {
						/* reset the flag, to make sure we stay in this mode if
						 * something is actually there */
						flag = FALSE;
						/* scan the interesting list and see if there in anything here */
						for (i = 0; i < point_set_size(targets); i++) {
							if ((y == targets->pts[i].y) && (x == targets->pts[i].x)) {
								m = i;
								flag = TRUE;
								break;
							}
						}
					} else {
						flag = FALSE;
					}
				}
			} else
			switch (press.key.code)
			{
				case ESCAPE:
				case 'q':
				{
					done = TRUE;
					break;
				}

				case ' ':
				case '*':
				case '+':
				{
					if (++m == point_set_size(targets))
						m = 0;

					break;
				}

				case '-':
				{
					if (m-- == 0)
						m = point_set_size(targets) - 1;

					break;
				}

				case 'p':
				{
					/* Recenter around player */
					verify_panel();

					/* Handle stuff */
					handle_stuff(player->upkeep);

					y = player->py;
					x = player->px;
				}

				case 'o':
				{
					flag = FALSE;
					break;
				}

				case 'm':
				{
					break;
				}

				case 't':
				case '5':
				case '0':
				case '.':
				{
					struct monster *m = square_monster(cave, y, x);

					if (target_able(m))
					{
						health_track(player->upkeep, m);
						target_set_monster(m);
						done = TRUE;
					}
					else
					{
						bell("Illegal target!");
					}
					break;
				}

				case 'g':
				{
					cmdq_push(CMD_PATHFIND);
					cmd_set_arg_point(cmdq_peek(), "point", y, x);
					done = TRUE;
					break;
				}
				
				case '?':
				{
					help = !help;
					
					/* Redraw main window */
					player->upkeep->redraw |= (PR_BASIC | PR_EXTRA | PR_MAP | PR_EQUIP);
					Term_clear();
					handle_stuff(player->upkeep);
					if (!help)
						prt("Press '?' for help.", help_prompt_loc, 0);
					
					break;
				}

				default:
				{
					/* Extract direction */
					d = target_dir(press.key);

					/* Oops */
					if (!d) bell("Illegal command for target mode!");

					break;
				}
			}

			/* Hack -- move around */
			if (d)
			{
				int old_y = targets->pts[m].y;
				int old_x = targets->pts[m].x;

				/* Find a new monster */
				i = target_pick(old_y, old_x, ddy[d], ddx[d], targets);

				/* Scroll to find interesting grid */
				if (i < 0)
				{
					int old_wy = Term->offset_y;
					int old_wx = Term->offset_x;

					/* Change if legal */
					if (change_panel(d))
					{
						/* Recalculate interesting grids */
						point_set_dispose(targets);
						targets = target_set_interactive_prepare(mode);

						/* Find a new monster */
						i = target_pick(old_y, old_x, ddy[d], ddx[d], targets);

						/* Restore panel if needed */
						if ((i < 0) && modify_panel(Term, old_wy, old_wx))
						{
							/* Recalculate interesting grids */
							point_set_dispose(targets);
							targets = target_set_interactive_prepare(mode);
						}

						/* Handle stuff */
						handle_stuff(player->upkeep);
					}
				}

				/* Use interesting grid if found */
				if (i >= 0) m = i;
			}
		}

		/* Arbitrary grids */
		else
		{
			/* Update help */
			if (help) 
			{
				bool good_target = target_able(square_monster(cave, y, x));
				target_display_help(good_target, !(flag && point_set_size(targets)));
			}

			/* Find the path. */
			path_n = project_path(path_g, MAX_RANGE, py, px, y, x, PROJECT_THRU);

			/* Draw the path in "target" mode. If there is one */
			if (mode & (TARGET_KILL))
				path_drawn = draw_path (path_n, path_g, path_char, path_attr, py, px);

			/* Describe and Prompt (enable "TARGET_LOOK") */
			press = target_set_interactive_aux(y, x, mode | TARGET_LOOK);

			/* Remove the path */
			if (path_drawn)  load_path(path_n, path_g, path_char, path_attr);

			/* Assume no direction */
			d = 0;

			/* Analyze the keypress */
			if (press.type == EVT_MOUSE) {
				if (press.mouse.button == 3) {
					/* give the target selection command */
					press.mouse.button = 2;
					press.mouse.mods = KC_MOD_CONTROL;
				}
				if (press.mouse.button == 2) {
					if (mode & (TARGET_KILL)) {
						if ((y == KEY_GRID_Y(press)) 
								&& (x == KEY_GRID_X(press))) {
							d = -1;
						}
					}
					y = KEY_GRID_Y(press);//.mouse.y;
					x = KEY_GRID_X(press);//.mouse.x;
					if (press.mouse.mods & KC_MOD_CONTROL) {
						/* same as keyboard target selection command below */
						target_set_location(y, x);
						done = TRUE;
					} else
					if (press.mouse.mods & KC_MOD_ALT) {
						/* go to spot - same as 'g' command below */
						cmdq_push(CMD_PATHFIND);
						cmd_set_arg_point(cmdq_peek(), "point", y, x);
						done = TRUE;
					} else
					{
						/* cancel look mode */
						done = TRUE;
						if (d == -1) {
							target_set_location(y, x);
							d = 0;
						}
					}
				} else
				/*if (press.mouse.button == 3) {
				} else*/
				{
					int dungeon_hgt = cave->height;
					int dungeon_wid = cave->width;

					y = KEY_GRID_Y(press);//.mouse.y;
					x = KEY_GRID_X(press);//.mouse.x;
				  
					if (press.mouse.y <= 1) {
						/* move the screen north */
						y--;
					} else
					if (press.mouse.y >= (Term->hgt - 2)) {
						/* move the screen south */
						y++;
					} else
					if (press.mouse.x <= COL_MAP) {
						/* move the screen in west */
						x--;
					} else
					if (press.mouse.x >= (Term->wid - 2)) {
						/* move the screen east */
						x++;
					}
          
					if (y < 0) y = 0;
					if (x < 0) x = 0;
					if (y >= dungeon_hgt-1) y = dungeon_hgt-1;
					if (x >= dungeon_wid-1) x = dungeon_wid-1;

					/* Adjust panel if needed */
					if (adjust_panel_help(y, x, help))
					{
						/* Handle stuff */
						handle_stuff(player->upkeep);

						/* Recalculate interesting grids */
						point_set_dispose(targets);
						targets = target_set_interactive_prepare(mode);
					}

					if (cave->m_idx[y][x] || cave->o_idx[y][x]) {
						/* scan the interesting list and see if there in anything here */
						for (i = 0; i < point_set_size(targets); i++) {
							if ((y == targets->pts[i].y) && (x == targets->pts[i].x)) {
								m = i;
								flag = TRUE;
								break;
							}
						}
					} else {
						flag = FALSE;
					}
				}
			} else
			switch (press.key.code)
			{
				case ESCAPE:
				case 'q':
				{
					done = TRUE;
					break;
				}

				case ' ':
				case '*':
				case '+':
				case '-':
				{
					break;
				}

				case 'p':
				{
					/* Recenter around player */
					verify_panel();

					/* Handle stuff */
					handle_stuff(player->upkeep);

					y = player->py;
					x = player->px;
				}

				case 'o':
				{
					break;
				}

				case 'm':
				{
					flag = TRUE;

					m = 0;
					bd = 999;

					/* Pick a nearby monster */
					for (i = 0; i < point_set_size(targets); i++)
					{
						t = distance(y, x, targets->pts[i].y, targets->pts[i].x);

						/* Pick closest */
						if (t < bd)
						{
							m = i;
							bd = t;
						}
					}

					/* Nothing interesting */
					if (bd == 999) flag = FALSE;

					break;
				}

				case 't':
				case '5':
				case '0':
				case '.':
				{
					target_set_location(y, x);
					done = TRUE;
					break;
				}

				case 'g':
				{
					cmdq_push(CMD_PATHFIND);
					cmd_set_arg_point(cmdq_peek(), "point", y, x);
					done = TRUE;
					break;
				}

				case '?':
				{
					help = !help;
					
					/* Redraw main window */
					player->upkeep->redraw |= (PR_BASIC | PR_EXTRA | PR_MAP | PR_EQUIP);
					Term_clear();
					handle_stuff(player->upkeep);
					if (!help)
						prt("Press '?' for help.", help_prompt_loc, 0);
					
					break;
				}

				default:
				{
					/* Extract a direction */
					d = target_dir(press.key);

					/* Oops */
					if (!d) bell("Illegal command for target mode!");

					break;
				}
			}

			/* Handle "direction" */
			if (d)
			{
				int dungeon_hgt = cave->height;
				int dungeon_wid = cave->width;

				/* Move */
				x += ddx[d];
				y += ddy[d];

				/* Slide into legality */
				if (x >= dungeon_wid - 1) x--;
				else if (x <= 0) x++;

				/* Slide into legality */
				if (y >= dungeon_hgt - 1) y--;
				else if (y <= 0) y++;

				/* Adjust panel if needed */
				if (adjust_panel_help(y, x, help))
				{
					/* Handle stuff */
					handle_stuff(player->upkeep);

					/* Recalculate interesting grids */
					point_set_dispose(targets);
					targets = target_set_interactive_prepare(mode);
				}
			}
		}
	}

	/* Forget */
	point_set_dispose(targets);

	/* Redraw as necessary */
	if (help)
	{
		player->upkeep->redraw |= (PR_BASIC | PR_EXTRA | PR_MAP | PR_EQUIP);
		Term_clear();
	}
	else
	{
		prt("", 0, 0);
		prt("", help_prompt_loc, 0);
		player->upkeep->redraw |= (PR_DEPTH | PR_STATUS);
	}

	/* Recenter around player */
	verify_panel();

	/* Handle stuff */
	handle_stuff(player->upkeep);

	/* Failure to set target */
	if (!target_set) return (FALSE);

	/* Success */
	return (TRUE);
}


/**
 * Obtains the location the player currently targets.
 *
 * Both `col` and `row` must point somewhere, and on function termination,
 * contain the X and Y locations respectively.
 */
void target_get(s16b *col, s16b *row)
{
	assert(col);
	assert(row);

	*col = target_x;
	*row = target_y;
}


/**
 * Returns the currently targeted monster index.
 */
struct monster *target_get_monster(void)
{
	return target_who;
}


/*
 * True if the player's current target is in LOS.
 */
bool target_sighted(void)
{
	return target_okay() &&
			panel_contains(target_y, target_x) &&
			 /* either the target is a grid and is visible, or it is a monster that is visible */
			((!target_who && player_can_see_bold(target_y, target_x)) || (target_who && target_who->ml));
}

