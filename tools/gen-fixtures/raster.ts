// A tiny RGB canvas with filled rectangles and a built-in 5×7 pixel font.
// The fixture generator draws pages with this instead of SVG text or system fonts: every pixel is set by our own code,
// so the same input gives the same bytes on any machine (sharp only encodes the finished pixels).

export type Rgb = readonly [number, number, number];

export class Canvas {
    readonly width: number;
    readonly height: number;
    readonly data: Uint8Array; // RGB, row by row

    constructor(width: number, height: number, background: Rgb) {
        this.width = width;
        this.height = height;
        this.data = new Uint8Array(width * height * 3);
        this.fillRect(0, 0, width, height, background);
    }

    fillRect(x: number, y: number, w: number, h: number, color: Rgb): void {
        const x0 = Math.max(0, Math.round(x)), y0 = Math.max(0, Math.round(y));
        const x1 = Math.min(this.width, Math.round(x + w)), y1 = Math.min(this.height, Math.round(y + h));
        for (let yy = y0; yy < y1; yy++) {
            let p = (yy * this.width + x0) * 3;
            for (let xx = x0; xx < x1; xx++, p += 3) {
                this.data[p] = color[0];
                this.data[p + 1] = color[1];
                this.data[p + 2] = color[2];
            }
        }
    }

    /** A border of thickness t just inside the rectangle. */
    strokeRect(x: number, y: number, w: number, h: number, t: number, color: Rgb): void {
        this.fillRect(x, y, w, t, color);
        this.fillRect(x, y + h - t, w, t, color);
        this.fillRect(x, y, t, h, color);
        this.fillRect(x + w - t, y, t, h, color);
    }

    /** Draws text with the 5×7 font, each font pixel scale×scale screen pixels. Lowercase is drawn as uppercase. */
    drawText(text: string, x: number, y: number, scale: number, color: Rgb): void {
        let cx = x;
        for (const ch of text.toUpperCase()) {
            const glyph = FONT[ch] ?? FONT['?'];
            for (let row = 0; row < 7; row++) {
                for (let col = 0; col < 5; col++) {
                    if (glyph[row][col] === '#') this.fillRect(cx + col * scale, y + row * scale, scale, scale, color);
                }
            }
            cx += 6 * scale;
        }
    }
}

/** Width in pixels of text drawn at this scale (no trailing gap). */
export function textWidth(text: string, scale: number): number {
    return text.length === 0 ? 0 : (text.length * 6 - 1) * scale;
}

/** Height in pixels of one line of text at this scale. */
export function textHeight(scale: number): number {
    return 7 * scale;
}

// 5×7 glyphs, '#' = ink. Classic LCD-style shapes, drawn by hand for this project.
const FONT: Record<string, readonly string[]> = {
    ' ': ['.....', '.....', '.....', '.....', '.....', '.....', '.....'],
    A: ['.###.', '#...#', '#...#', '#####', '#...#', '#...#', '#...#'],
    B: ['####.', '#...#', '#...#', '####.', '#...#', '#...#', '####.'],
    C: ['.###.', '#...#', '#....', '#....', '#....', '#...#', '.###.'],
    D: ['###..', '#..#.', '#...#', '#...#', '#...#', '#..#.', '###..'],
    E: ['#####', '#....', '#....', '####.', '#....', '#....', '#####'],
    F: ['#####', '#....', '#....', '####.', '#....', '#....', '#....'],
    G: ['.###.', '#...#', '#....', '#.###', '#...#', '#...#', '.####'],
    H: ['#...#', '#...#', '#...#', '#####', '#...#', '#...#', '#...#'],
    I: ['.###.', '..#..', '..#..', '..#..', '..#..', '..#..', '.###.'],
    J: ['..###', '...#.', '...#.', '...#.', '...#.', '#..#.', '.##..'],
    K: ['#...#', '#..#.', '#.#..', '##...', '#.#..', '#..#.', '#...#'],
    L: ['#....', '#....', '#....', '#....', '#....', '#....', '#####'],
    M: ['#...#', '##.##', '#.#.#', '#.#.#', '#...#', '#...#', '#...#'],
    N: ['#...#', '#...#', '##..#', '#.#.#', '#..##', '#...#', '#...#'],
    O: ['.###.', '#...#', '#...#', '#...#', '#...#', '#...#', '.###.'],
    P: ['####.', '#...#', '#...#', '####.', '#....', '#....', '#....'],
    Q: ['.###.', '#...#', '#...#', '#...#', '#.#.#', '#..#.', '.##.#'],
    R: ['####.', '#...#', '#...#', '####.', '#.#..', '#..#.', '#...#'],
    S: ['.####', '#....', '#....', '.###.', '....#', '....#', '####.'],
    T: ['#####', '..#..', '..#..', '..#..', '..#..', '..#..', '..#..'],
    U: ['#...#', '#...#', '#...#', '#...#', '#...#', '#...#', '.###.'],
    V: ['#...#', '#...#', '#...#', '#...#', '#...#', '.#.#.', '..#..'],
    W: ['#...#', '#...#', '#...#', '#.#.#', '#.#.#', '#.#.#', '.#.#.'],
    X: ['#...#', '#...#', '.#.#.', '..#..', '.#.#.', '#...#', '#...#'],
    Y: ['#...#', '#...#', '.#.#.', '..#..', '..#..', '..#..', '..#..'],
    Z: ['#####', '....#', '...#.', '..#..', '.#...', '#....', '#####'],
    '0': ['.###.', '#...#', '#..##', '#.#.#', '##..#', '#...#', '.###.'],
    '1': ['..#..', '.##..', '..#..', '..#..', '..#..', '..#..', '.###.'],
    '2': ['.###.', '#...#', '....#', '...#.', '..#..', '.#...', '#####'],
    '3': ['#####', '...#.', '..#..', '...#.', '....#', '#...#', '.###.'],
    '4': ['...#.', '..##.', '.#.#.', '#..#.', '#####', '...#.', '...#.'],
    '5': ['#####', '#....', '####.', '....#', '....#', '#...#', '.###.'],
    '6': ['..##.', '.#...', '#....', '####.', '#...#', '#...#', '.###.'],
    '7': ['#####', '....#', '...#.', '..#..', '.#...', '.#...', '.#...'],
    '8': ['.###.', '#...#', '#...#', '.###.', '#...#', '#...#', '.###.'],
    '9': ['.###.', '#...#', '#...#', '.####', '....#', '...#.', '.##..'],
    '.': ['.....', '.....', '.....', '.....', '.....', '.##..', '.##..'],
    ':': ['.....', '.##..', '.##..', '.....', '.##..', '.##..', '.....'],
    '-': ['.....', '.....', '.....', '#####', '.....', '.....', '.....'],
    '/': ['....#', '....#', '...#.', '..#..', '.#...', '#....', '#....'],
    '#': ['.#.#.', '.#.#.', '#####', '.#.#.', '#####', '.#.#.', '.#.#.'],
    '<': ['...#.', '..#..', '.#...', '#....', '.#...', '..#..', '...#.'],
    '>': ['.#...', '..#..', '...#.', '....#', '...#.', '..#..', '.#...'],
    '(': ['...#.', '..#..', '.#...', '.#...', '.#...', '..#..', '...#.'],
    ')': ['.#...', '..#..', '...#.', '...#.', '...#.', '..#..', '.#...'],
    '|': ['..#..', '..#..', '..#..', '..#..', '..#..', '..#..', '..#..'],
    '?': ['.###.', '#...#', '....#', '...#.', '..#..', '.....', '..#..'],
};
