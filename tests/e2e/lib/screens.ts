// Where things are on the app's screens (WP-08), in screen pixels (1280x720), for touch. Read from the desktop screenshots;
// the Switch draws the same layout. Rows of cells and tabs are 72 px apart (D16: 72 px touch targets).

/** The main screen's tabs, top to bottom (MainModel::tabs()). */
export const TABS = ['Library', 'Updates', 'History', 'Browse', 'Settings'] as const;
export function tab(name: (typeof TABS)[number]): { x: number; y: number } {
    return { x: 225, y: 156 + 72 * TABS.indexOf(name) };
}

/** Settings tab rows (ui_login shows "Log out" as the third row; the other modes don't have it). */
export function settingsRow(row: 'address' | 'login' | 'logout' | 'serverVersion', uiLogin: boolean): { x: number; y: number } {
    const order = uiLogin ? ['address', 'login', 'logout', 'serverVersion'] : ['address', 'login', 'serverVersion'];
    return { x: 845, y: 206 + 72 * order.indexOf(row) };
}

/** The setup screen without username/password rows (login "none"). */
export const SETUP = { url: { x: 640, y: 184 }, test: { x: 369, y: 352 }, continue: { x: 941, y: 352 } };

/** The login screen: username, password, then the buttons. */
export const LOGIN = { login: { x: 369, y: 352 } };

/**
 * The footer's button hints (bottom right, borealis): a tap on one does what its button does (WP-12 touch pass). "Back" sits
 * just left of "OK", the last hint, on every screen that has both.
 */
export const FOOTER = { back: { x: 1070, y: 683 } };

/** The footer's clock (bottom left). On the Switch the same corner has battery and Wi-Fi. */
export const CLOCK_MASK = { x: 0, y: 650, w: 420, h: 70, why: 'clock, battery, Wi-Fi' };
/** WP-24: the Library's category tabs once scrolled (to Empty): the scroll stops a pixel apart from run to run. */
export const SCROLLED_TABS_MASK = { x: 410, y: 80, w: 420, h: 92, why: 'category tabs scrolled by the focus' };

/**
 * Desktop only: the "OK" hint of borealis's text dialog (the keyboard on the PC). A tap on it submits. Enter doesn't work
 * there: while the dialog is open, GLFW hands keys to the input method, which swallows Return.
 */
export const KEYBOARD_OK = { x: 937, y: 218 };

/**
 * The Library tab (WP-09): the category bar (y 123: the test server's categories, then Sort and Refresh) and the cover grid
 * (6 columns of 120 x 232 px cells, 16 px apart; with the grid at the top the first cover's top-left corner is 445,184).
 */
export const LIBRARY = {
    categories: {
        default: { x: 481, y: 123 },
        reading: { x: 587, y: 123 },
        longSeries: { x: 708, y: 123 },
        bigShelf: { x: 815, y: 123 },  // partly hidden since WP-24 (Search, Edit); Empty is off-screen: RIGHT from here
    },
    sort: { x: 918, y: 123 },
    search: { x: 1032, y: 123 },  // WP-24
    edit: { x: 1120, y: 123 },  // WP-24: the Categories screen
    refresh: { x: 1211, y: 123 },
    /** The sort menu (borealis dropdown): Title, Unread, Last read. */
    sortMenu: { unread: { x: 640, y: 516 } },
    /** The centre of cover `index`'s image, while the grid is scrolled to the top (first two rows). */
    cover(index: number): { x: number; y: number } {
        return { x: 445 + 60 + 136 * (index % 6), y: 184 + 90 + 248 * Math.floor(index / 6) };
    },
    /** A flick up over the grid scrolls down (and the other way). How far one goes depends on its speed, so scenarios
     * flick until the grid says it got there, up to maxFlicks times. */
    flickFrom: { x: 845, y: 600 },
    flickTo: { x: 845, y: 220 },
    maxFlicks: 40,
};

/**
 * The Retry button of the error message (StatusView, WP-08) where a screen has nothing else to show (WP-12): in the Library
 * tab (right of the tab list) and on the manga details screen (where the chapter list goes). The message above it takes one
 * or two lines, which moves the button by ~18 px; these points are on it either way.
 */
export const STATUS = {
    library: { x: 845, y: 400 },
    manga: { x: 860, y: 440 },
};

/** The Library tab's other category tabs (the test server's order: Default, Reading, Long series, Big shelf, Empty). */
export const LIBRARY_MORE = {
    reading: { x: 587, y: 123 },
    longSeries: { x: 708, y: 123 },
};

/**
 * The Updates and History tabs (WP-15): one column of 800 x 96 px rows (8 px apart), a small cover on the left, the day on
 * the right, Updates rows end in a "..." (the row menu). Updates has a header above the rows: the Update library / Stop
 * button and the status line with its progress bar.
 */
export const UPDATES = {
    updateButton: { x: 544, y: 136 },
    /** The middle of row `index` (a tap opens the chapter), while the list is scrolled to the top. */
    row(index: number, header = true): { x: number; y: number } {
        return { x: 800, y: (header ? 244 : 148) + 104 * index };
    },
    /** The cover of row `index` (a tap opens the details screen). */
    cover(index: number, header = true): { x: number; y: number } {
        return { x: 483, y: (header ? 244 : 148) + 104 * index };
    },
    /** The status line (its text has the time or how long the update took). */
    statusMask: { x: 670, y: 100, w: 600, h: 72, why: 'the status line: time, duration' },
};

/**
 * The manga details screen (WP-10). Left: Continue and the description. Right: the bar (Sort, Show, Refresh) and the
 * chapter list (rows of 800 x 80 px, 4 px apart; with the list at the top the first row's top is at y 167).
 */
export const MANGA = {
    continue: { x: 228, y: 343 },
    categories: { x: 228, y: 427 },  // WP-24: "Categories: …" opens the category picker
    description: { x: 228, y: 540 },  // below the Categories button since WP-24
    sort: { x: 942, y: 123 },
    filter: { x: 1100, y: 123 },
    refresh: { x: 1211, y: 123 },
    /** The middle of row `index` (a tap opens the chapter), while the list is scrolled to the top. */
    row(index: number): { x: number; y: number } {
        return { x: 760, y: 207 + 84 * index };
    },
    /** The "..." at the end of row `index` (a tap opens the chapter's menu). */
    rowMenu(index: number): { x: number; y: number } {
        return { x: 1224, y: 207 + 84 * index };
    },
    /**
     * Entry `index` of a borealis dropdown with `count` entries (they sit at the bottom of the screen, 60 px apart, the last
     * one's middle at y 576; measured with 5 entries in WP-22, where the old 72 px guess missed the first entry).
     */
    menuEntry(index: number, count: number): { x: number; y: number } {
        return { x: 640, y: 576 - 60 * (count - 1 - index) };
    },
};

/**
 * WP-24: the Categories screen (Library -> Edit) and the category picker (manga details -> Categories). Both: a note and
 * buttons on top, then a one-column list; the row menus are borealis dropdowns (MANGA.menuEntry).
 */
export const CATEGORIES = {
    add: { x: 1169, y: 136 },
    /** The middle of row `index` (a tap opens its menu: Rename, Move up, Move down, Delete). */
    row(index: number): { x: number; y: number } {
        return { x: 640, y: 224 + 84 * index };
    },
};

export const PICKER = {
    newCategory: { x: 1041, y: 136 },
    save: { x: 1167, y: 136 },
    /** The middle of row `index` (a tap ticks or unticks it). */
    row(index: number): { x: number; y: number } {
        return { x: 640, y: 224 + 84 * index };
    },
};

/** A borealis dialog with two buttons (WP-24's delete question): Cancel on the left, the action on the right. */
export const DIALOG = {
    cancel: { x: 460, y: 456 },
    confirm: { x: 820, y: 456 },
};

/**
 * The reader (WP-11), full screen. Taps: the outer 30 % on each side turn pages (mirrored for right to left), the middle
 * opens and closes the menu (the middle tap is high up, clear of the Retry button). With the menu open: Close and Settings
 * in the bottom bar. Swipes: "toLeft" = the finger moves to the left (next page when reading left to right).
 */
export const READER = {
    left: { x: 120, y: 360 },
    middle: { x: 640, y: 200 },
    right: { x: 1160, y: 360 },
    close: { x: 132, y: 672 },
    settings: { x: 1148, y: 672 },
    retry: { x: 640, y: 409 },
    swipeToLeft: { x1: 900, y1: 380, x2: 300, y2: 380 },
    swipeToRight: { x1: 300, y1: 380, x2: 900, y2: 380 },
    /** WP-22: the menu's Rotate button (between the page number and Settings). */
    rotate: { x: 766, y: 672 },
    /**
     * The settings menu's entries (a borealis dropdown, MANGA.menuEntry places them): direction, fit, background, number,
     * orientation (WP-22), zoom on page turn (WP-13), split spreads (WP-13c). With 7 the reader makes the menu tall enough
     * for all of them (borealis would cap it at 6 and scroll), so the last one's middle stays at y 576.
     */
    settingsCount: 7,
};

/** WP-22: how the console is held in the reader ("reader" events' orientation). */
export type Orientation = 'landscape' | 'portrait-left' | 'portrait-right';

/**
 * The reader in portrait (WP-22), in reader units on its 720 x 1280 screen: tap zones (the outer 30 % on each side), the
 * menu's buttons (Close, Rotate, Settings in the bottom bar), Retry, and swipes. Turn them into the landscape touch points
 * the drivers send with portraitPoint().
 */
export const PORTRAIT = {
    left: { x: 100, y: 640 },
    middle: { x: 360, y: 400 },
    right: { x: 620, y: 640 },
    close: { x: 100, y: 1232 },
    rotate: { x: 414, y: 1232 },
    settings: { x: 620, y: 1232 },
    swipeToLeft: { x1: 600, y1: 700, x2: 150, y2: 700 },
    swipeToRight: { x1: 150, y1: 700, x2: 600, y2: 700 },
};

/**
 * A point on the portrait reader's screen -> the landscape screen point to touch (core::readerToScreen). Turned left: the
 * reader's top is the real right edge; turned right: its top is the real left edge.
 */
export function portraitPoint(way: Orientation, p: { x: number; y: number }): { x: number; y: number } {
    if (way === 'portrait-left') {
        return { x: 1280 - p.y, y: p.x };
    }
    if (way === 'portrait-right') {
        return { x: p.y, y: 720 - p.x };
    }
    return p;
}
