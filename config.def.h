static const Bool wmborder = True;
static int fontsize = 22;
static double overlay_delay = 1.0;
static int heightfactor = 14; //one row of keys takes up 1/x of the screen height
static const char *fonts[] = {
	"DejaVu Sans:bold:size=22"
};
static const char *colors[SchemeLast][2] = {
	/*     fg         bg       */
	[SchemeNorm] = { "#bbbbbb", "#132a33" },
	[SchemeNormABC] = { "#ffffff", "#14313d" },
	[SchemePress] = { "#ffffff", "#000000" },
	[SchemeHighlight] = { "#58a7c6", "#005577" },
};
