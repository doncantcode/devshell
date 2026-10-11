#ifndef TOUR_H
#define TOUR_H

/* `tour`: an interactive walkthrough. Each step explains one idea and waits
 * for the user to type a matching command, which really runs. Everything
 * happens inside a throw-away scratch directory that is removed afterwards.
 * Returns an exit status. */
int tour_run(void);

#endif
