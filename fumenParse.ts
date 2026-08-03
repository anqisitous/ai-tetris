// ============================================================
// 定数定義
// ============================================================
export enum Piece {
  Empty = 0, I = 1, L = 2, O = 3, Z = 4, T = 5, J = 6, S = 7, Gray = 8,
}

export type PieceType = 'I' | 'L' | 'O' | 'Z' | 'T' | 'J' | 'S' | 'X' | '_';

export enum Rotation {
  Spawn = 0, Right = 1, Reverse = 2, Left = 3,
}

export type RotationType = 'spawn' | 'right' | 'reverse' | 'left';

export function parsePiece(piece: string): Piece {
  const p = piece.toUpperCase();
  if (p === 'I') return Piece.I;
  if (p === 'L') return Piece.L;
  if (p === 'O') return Piece.O;
  if (p === 'Z') return Piece.Z;
  if (p === 'T') return Piece.T;
  if (p === 'J') return Piece.J;
  if (p === 'S') return Piece.S;
  if (p === 'X' || p === 'GRAY') return Piece.Gray;
  if (p === ' ' || p === '_' || p === 'EMPTY') return Piece.Empty;
  throw new Error(`Unknown piece: ${piece}`);
}

export function parsePieceName(piece: Piece): PieceType {
  const map: Record<Piece, PieceType> = {
    [Piece.Empty]: '_', [Piece.I]: 'I', [Piece.L]: 'L', [Piece.O]: 'O',
    [Piece.Z]: 'Z', [Piece.T]: 'T', [Piece.J]: 'J', [Piece.S]: 'S', [Piece.Gray]: 'X',
  };
  return map[piece];
}

export function parseRotation(rotation: RotationType): Rotation {
  const map: Record<RotationType, Rotation> = {
    spawn: Rotation.Spawn, left: Rotation.Left, right: Rotation.Right, reverse: Rotation.Reverse,
  };
  return map[rotation];
}

export function parseRotationName(rotation: Rotation): RotationType {
  const map: Record<Rotation, RotationType> = {
    [Rotation.Spawn]: 'spawn', [Rotation.Left]: 'left',
    [Rotation.Right]: 'right', [Rotation.Reverse]: 'reverse',
  };
  return map[rotation];
}

export function isMinoPiece(piece: Piece) {
  return piece !== Piece.Empty && piece !== Piece.Gray;
}

export interface InnerOperation {
  type: Piece;
  rotation: Rotation;
  x: number;
  y: number;
}

// ============================================================
// バッファ (Base64エンコード/デコード)
// ============================================================
const ENCODE_TABLE = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';

export class Buffer {
  static readonly tableLength = ENCODE_TABLE.length;
  private values: number[];

  constructor(data: string = '') {
    this.values = data.split('').map(c => ENCODE_TABLE.indexOf(c));
  }

  poll(max: number): number {
    let value = 0;
    for (let i = 0; i < max; i++) {
      const v = this.values.shift();
      if (v === undefined) throw new Error('Unexpected fumen');
      value += v * Math.pow(Buffer.tableLength, i);
    }
    return value;
  }

  push(value: number, splitCount: number = 1): void {
    let current = value;
    for (let i = 0; i < splitCount; i++) {
      this.values.push(current % Buffer.tableLength);
      current = Math.floor(current / Buffer.tableLength);
    }
  }

  merge(post: Buffer): void {
    for (const v of post.values) this.values.push(v);
  }

  isEmpty(): boolean { return this.values.length === 0; }
  get length(): number { return this.values.length; }
  get(index: number): number { return this.values[index]; }
  set(index: number, value: number): void { this.values[index] = value; }
  toString(): string { return this.values.map(i => ENCODE_TABLE[i]).join(''); }
}

// ============================================================
// フィールド (内部表現)
// ============================================================
const W = 10, H = 23, BLOCKS = H * W;

export class PlayField {
  private pieces: Piece[];

  constructor(length: number = BLOCKS) {
    this.pieces = Array.from({ length }).map(() => Piece.Empty);
  }

  get(x: number, y: number): Piece {
    return this.pieces[x + y * W];
  }
  set(x: number, y: number, piece: Piece): void {
    this.pieces[x + y * W] = piece;
  }
  copy(): PlayField {
    const f = new PlayField();
    f.pieces = this.pieces.concat();
    return f;
  }
  toArray(): Piece[] { return this.pieces.concat(); }
  equals(other: PlayField): boolean {
    return this.pieces.every((v, i) => v === other.pieces[i]);
  }
  clearAll(): void {
    this.pieces = this.pieces.map(() => Piece.Empty);
  }

  fillAll(positions: { x: number; y: number }[], type: Piece): void {
    for (const { x, y } of positions) this.set(x, y, type);
  }

  clearLine(): void {
    let newField = this.pieces.concat();
    for (let y = H - 1; y >= 0; y--) {
      const line = this.pieces.slice(y * W, (y + 1) * W);
      if (line.every(v => v !== Piece.Empty)) {
        const bottom = newField.slice(0, y * W);
        const over = newField.slice((y + 1) * W);
        newField = bottom.concat(over, Array.from({ length: W }).map(() => Piece.Empty));
      }
    }
    this.pieces = newField;
  }

  rise(garbage: PlayField): void {
    this.pieces = garbage.pieces.concat(this.pieces).slice(0, BLOCKS);
  }

  mirror(): void {
    const newField: Piece[] = [];
    for (let y = 0; y < H; y++) {
      const line = this.pieces.slice(y * W, (y + 1) * W).reverse();
      newField.push(...line);
    }
    this.pieces = newField;
  }
}

export class InnerField {
  private field: PlayField;
  private garbage: PlayField;

  constructor() {
    this.field = new PlayField();
    this.garbage = new PlayField(W);
  }

  getNumberAt(x: number, y: number): Piece {
    return y >= 0 ? this.field.get(x, y) : this.garbage.get(x, -(y + 1));
  }
  setNumberAt(x: number, y: number, value: number): void {
    if (y >= 0) this.field.set(x, y, value);
    else this.garbage.set(x, -(y + 1), value);
  }
  addNumber(x: number, y: number, value: number): void {
    if (y >= 0) this.field.set(x, y, this.field.get(x, y) + value);
    else this.garbage.set(x, -(y + 1), this.garbage.get(x, -(y + 1)) + value);
  }

  fillAll(positions: { x: number; y: number }[], type: Piece): void {
    this.field.fillAll(positions, type);
  }
  clearLine(): void { this.field.clearLine(); }
  riseGarbage(): void {
    this.field.rise(this.garbage);
    this.garbage.clearAll();
  }
  mirror(): void { this.field.mirror(); }

  canFillAll(positions: { x: number; y: number }[]): boolean {
    return positions.every(({ x, y }) => {
      return x >= 0 && x < W && y >= 0 && y < H && this.getNumberAt(x, y) === Piece.Empty;
    });
  }

  copy(): InnerField {
    const f = new InnerField();
    f.field = this.field.copy();
    f.garbage = this.garbage.copy();
    return f;
  }

  equals(other: InnerField): boolean {
    return this.field.equals(other.field) && this.garbage.equals(other.garbage);
  }

  toFieldNumberArray(): Piece[] { return this.field.toArray(); }
  toGarbageNumberArray(): Piece[] { return this.garbage.toArray(); }
}

// ============================================================
// ブロック座標
// ============================================================
function getPieces(piece: Piece): number[][] {
  const map: Record<Piece, number[][]> = {
    [Piece.I]: [[0,0],[-1,0],[1,0],[2,0]],
    [Piece.T]: [[0,0],[-1,0],[1,0],[0,1]],
    [Piece.O]: [[0,0],[1,0],[0,1],[1,1]],
    [Piece.L]: [[0,0],[-1,0],[1,0],[1,1]],
    [Piece.J]: [[0,0],[-1,0],[1,0],[-1,1]],
    [Piece.S]: [[0,0],[-1,0],[0,1],[1,1]],
    [Piece.Z]: [[0,0],[1,0],[0,1],[-1,1]],
    [Piece.Empty]: [], [Piece.Gray]: [],
  };
  return map[piece];
}

function rotateRight(pos: number[][]): number[][] {
  return pos.map(([x, y]) => [y, -x]);
}
function rotateLeft(pos: number[][]): number[][] {
  return pos.map(([x, y]) => [-y, x]);
}
function rotateReverse(pos: number[][]): number[][] {
  return pos.map(([x, y]) => [-x, -y]);
}

export function getBlocks(piece: Piece, rotation: Rotation): number[][] {
  const blocks = getPieces(piece);
  const map: Record<Rotation, (p: number[][]) => number[][]> = {
    [Rotation.Spawn]: p => p,
    [Rotation.Left]: rotateLeft,
    [Rotation.Reverse]: rotateReverse,
    [Rotation.Right]: rotateRight,
  };
  return map[rotation](blocks);
}

export function getBlockXYs(piece: Piece, rotation: Rotation, x: number, y: number): { x: number; y: number }[] {
  return getBlocks(piece, rotation).map(([bx, by]) => ({ x: x + bx, y: y + by }));
}

// ============================================================
// フィールド (公開API)
// ============================================================
export interface Operation {
  type: PieceType;
  rotation: RotationType;
  x: number;
  y: number;
}

export class Mino {
  constructor(
    public type: PieceType,
    public rotation: RotationType,
    public x: number,
    public y: number,
  ) {}

  positions(): { x: number; y: number }[] {
    return getBlockXYs(parsePiece(this.type), parseRotation(this.rotation), this.x, this.y);
  }

  operation(): Operation {
    return { type: this.type, rotation: this.rotation, x: this.x, y: this.y };
  }

  copy(): Mino {
    return new Mino(this.type, this.rotation, this.x, this.y);
  }
}

export class Field {
  constructor(private field: InnerField = new InnerField()) {}

  static create(fieldStr?: string): Field {
    const f = new Field();
    if (fieldStr) {
      const blocks = fieldStr.replace(/\s/g, '');
      for (let i = 0; i < blocks.length; i++) {
        const x = i % W;
        const y = H - 1 - Math.floor(i / W);
        f.field.setNumberAt(x, y, parsePiece(blocks[i]));
      }
    }
    return f;
  }

  copy(): Field { return new Field(this.field.copy()); }
  at(x: number, y: number): PieceType {
    return parsePieceName(this.field.getNumberAt(x, y));
  }

  canFill(operation?: Operation | Mino): boolean {
    if (!operation) return true;
    const mino = operation instanceof Mino ? operation : Mino.from(operation);
    return this.field.canFillAll(mino.positions());
  }

  fill(operation?: Operation | Mino): Mino | undefined {
    if (!operation) return undefined;
    const mino = operation instanceof Mino ? operation : Mino.from(operation);
    this.field.fillAll(mino.positions(), parsePiece(mino.type));
    return mino;
  }

  clearLine(): void { this.field.clearLine(); }
  mirror(): void { this.field.mirror(); }
  riseGarbage(): void { this.field.riseGarbage(); }

  str(): string {
    let out = '';
    for (let y = H - 1; y >= 0; y--) {
      for (let x = 0; x < W; x++) out += this.at(x, y);
      out += '\n';
    }
    return out.trim();
  }
}

// ============================================================
// アクションエンコーダ/デコーダ
// ============================================================
interface Action {
  piece: InnerOperation;
  rise: boolean;
  mirror: boolean;
  colorize: boolean;
  comment: boolean;
  lock: boolean;
}

const NUM_FIELD_BLOCKS = H * W;

function encodePosition(op: InnerOperation): number {
  const { type, rotation, x, y } = op;
  let nx = x, ny = y;
  if (!isMinoPiece(type)) { nx = 0; ny = 22; }
  else if (type === Piece.O && rotation === Rotation.Left) { nx -= 1; ny += 1; }
  else if (type === Piece.O && rotation === Rotation.Reverse) { nx -= 1; }
  else if (type === Piece.O && rotation === Rotation.Spawn) { ny += 1; }
  else if (type === Piece.I && rotation === Rotation.Reverse) { nx -= 1; }
  else if (type === Piece.I && rotation === Rotation.Left) { ny += 1; }
  else if (type === Piece.S && rotation === Rotation.Spawn) { ny += 1; }
  else if (type === Piece.S && rotation === Rotation.Right) { nx += 1; }
  else if (type === Piece.Z && rotation === Rotation.Spawn) { ny += 1; }
  else if (type === Piece.Z && rotation === Rotation.Left) { nx -= 1; }
  return (H - ny - 1) * W + nx;
}

function decodePosition(value: number, type: Piece, rotation: Rotation): { x: number; y: number } {
  let x = value % W;
  let y = H - Math.floor(value / W) - 1;
  if (type === Piece.O && rotation === Rotation.Left) { x += 1; y -= 1; }
  else if (type === Piece.O && rotation === Rotation.Reverse) { x += 1; }
  else if (type === Piece.O && rotation === Rotation.Spawn) { y -= 1; }
  else if (type === Piece.I && rotation === Rotation.Reverse) { x += 1; }
  else if (type === Piece.I && rotation === Rotation.Left) { y -= 1; }
  else if (type === Piece.S && rotation === Rotation.Spawn) { y -= 1; }
  else if (type === Piece.S && rotation === Rotation.Right) { x -= 1; }
  else if (type === Piece.Z && rotation === Rotation.Spawn) { y -= 1; }
  else if (type === Piece.Z && rotation === Rotation.Left) { x += 1; }
  return { x, y };
}

function encodeRotation(op: { type: Piece; rotation: Rotation }): number {
  if (!isMinoPiece(op.type)) return 0;
  const map: Record<Rotation, number> = {
    [Rotation.Reverse]: 0, [Rotation.Right]: 1, [Rotation.Spawn]: 2, [Rotation.Left]: 3,
  };
  return map[op.rotation];
}

function decodeRotation(value: number): Rotation {
  const map: Record<number, Rotation> = { 0: Rotation.Reverse, 1: Rotation.Right, 2: Rotation.Spawn, 3: Rotation.Left };
  return map[value];
}

function decodePiece(value: number): Piece {
  const map: Record<number, Piece> = {
    0: Piece.Empty, 1: Piece.I, 2: Piece.L, 3: Piece.O, 4: Piece.Z, 5: Piece.T, 6: Piece.J, 7: Piece.S, 8: Piece.Gray,
  };
  return map[value];
}

function encodeBool(flag: boolean): number { return flag ? 1 : 0; }

export function encodeAction(action: Action): number {
  const { lock, comment, colorize, mirror, rise, piece } = action;
  let value = encodeBool(!lock);
  value = value * 2 + encodeBool(comment);
  value = value * 2 + encodeBool(colorize);
  value = value * 2 + encodeBool(mirror);
  value = value * 2 + encodeBool(rise);
  value = value * NUM_FIELD_BLOCKS + encodePosition(piece);
  value = value * 4 + encodeRotation(piece);
  value = value * 8 + piece.type;
  return value;
}

export function decodeAction(value: number): Action {
  let v = value;
  const type = decodePiece(v % 8);
  v = Math.floor(v / 8);
  const rotation = decodeRotation(v % 4);
  v = Math.floor(v / 4);
  const coord = decodePosition(v % NUM_FIELD_BLOCKS, type, rotation);
  v = Math.floor(v / NUM_FIELD_BLOCKS);
  const rise = (v % 2) === 1;
  v = Math.floor(v / 2);
  const mirror = (v % 2) === 1;
  v = Math.floor(v / 2);
  const colorize = (v % 2) === 1;
  v = Math.floor(v / 2);
  const comment = (v % 2) === 1;
  v = Math.floor(v / 2);
  const lock = (v % 2) === 0;
  return {
    rise, mirror, colorize, comment, lock,
    piece: { ...coord, type, rotation },
  };
}

// ============================================================
// コメントエンコーダ/デコーダ
// ============================================================
const COMMENT_TABLE = ' !"#$%&\'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~';
const MAX_COMMENT_CHAR = COMMENT_TABLE.length + 1;

export function encodeComment(text: string): number[] {
  const result: number[] = [];
  for (let i = 0; i < text.length; i += 4) {
    let value = 0;
    for (let j = 0; j < 4 && i + j < text.length; j++) {
      value += COMMENT_TABLE.indexOf(text[i + j]) * Math.pow(MAX_COMMENT_CHAR, j);
    }
    result.push(value);
  }
  return result;
}

export function decodeComment(values: number[]): string {
  let result = '';
  for (const v of values) {
    let value = v;
    for (let i = 0; i < 4; i++) {
      result += COMMENT_TABLE[value % MAX_COMMENT_CHAR];
      value = Math.floor(value / MAX_COMMENT_CHAR);
    }
  }
  return result;
}

// ============================================================
// Quiz (ネクスト管理)
// ============================================================
enum OperationType {
  Direct = 'direct', Swap = 'swap', Stock = 'stock',
}

export class Quiz {
  private quiz: string;

  constructor(quiz: string) {
    this.quiz = this.trim(quiz);
  }

  private trim(s: string): string {
    return s.trim().replace(/\s+/g, '');
  }

  private get current(): string {
    const idx = this.quiz.indexOf('(') + 1;
    const ch = this.quiz[idx];
    return ch === ')' ? '' : ch;
  }

  private get hold(): string {
    const idx = this.quiz.indexOf('[') + 1;
    const ch = this.quiz[idx];
    return ch === ']' ? '' : ch;
  }

  private get next(): string | undefined {
    const idx = this.quiz.indexOf(')') + 1;
    const ch = this.quiz[idx];
    if (ch === undefined || ch === ';') return '';
    return ch;
  }

  private get leastAfterNext2(): string {
    const idx = this.quiz.indexOf(')');
    return this.quiz[idx + 1] === ';' ? this.quiz.substr(idx + 1) : this.quiz.substr(idx + 2);
  }

  static isQuizComment(comment: string): boolean {
    return comment.startsWith('#Q=');
  }

  canOperate(): boolean {
    let q = this.quiz;
    if (q.startsWith('#Q=[]();')) q = q.substr(8);
    return q.startsWith('#Q=') && q !== '#Q=[]()';
  }

  nextIfEnd(): Quiz {
    if (this.quiz.startsWith('#Q=[]();')) return new Quiz(this.quiz.substr(8));
    return this;
  }

  getOperation(used: Piece): OperationType {
    const usedName = parsePieceName(used);
    const current = this.current;
    if (usedName === current) return OperationType.Direct;
    if (usedName === this.hold) return OperationType.Swap;
    if (this.hold === '') {
      if (usedName === this.next) return OperationType.Stock;
    } else {
      if (current === '' && usedName === this.next) return OperationType.Direct;
    }
    throw new Error(`Unexpected hold piece in quiz: ${this.quiz}`);
  }

  operate(op: OperationType): Quiz {
    switch (op) {
      case OperationType.Direct: {
        if (this.current === '') {
          const least = this.leastAfterNext2;
          return new Quiz(`#Q=[${this.hold}](${least[0]})${least.substr(1)}`);
        }
        return new Quiz(`#Q=[${this.hold}](${this.next})${this.leastAfterNext2}`);
      }
      case OperationType.Swap: {
        if (this.hold === '') throw new Error(`Cannot find hold piece: ${this.quiz}`);
        return new Quiz(`#Q=[${this.current}](${this.next})${this.leastAfterNext2}`);
      }
      case OperationType.Stock: {
        if (this.hold !== '' || this.next === '') throw new Error(`Cannot stock: ${this.quiz}`);
        const least = this.leastAfterNext2;
        const head = least[0] || '';
        return new Quiz(`#Q=[${this.current}](${head})${least.substr(1)}`);
      }
    }
  }

  format(): Quiz {
    const q = this.nextIfEnd();
    if (q.quiz === '#Q=[]()') return new Quiz('');
    const current = q.current;
    const hold = q.hold;
    if (current === '' && hold !== '') return new Quiz(`#Q=[](${hold})${q.leastAfterNext2}`);
    if (current === '') {
      const least = q.leastAfterNext2;
      const head = least[0];
      if (head === undefined) return new Quiz('');
      if (head === ';') return new Quiz(least.substr(1));
      return new Quiz(`#Q=[](${head})${least.substr(1)}`);
    }
    return q;
  }

  toString(): string { return this.quiz; }
}

// ============================================================
// フィールド差分エンコード
// ============================================================
function encodeFieldDiff(prev: InnerField, current: InnerField): { changed: boolean; buffer: Buffer } {
  const buf = new Buffer();
  let prevDiff = current.getNumberAt(0, H - 1) - prev.getNumberAt(0, H - 1) + 8;
  let counter = -1;
  let changed = true;

  for (let y = H - 1; y >= 0; y--) {
    for (let x = 0; x < W; x++) {
      const diff = current.getNumberAt(x, y) - prev.getNumberAt(x, y) + 8;
      if (diff !== prevDiff) {
        buf.push(prevDiff * BLOCKS + counter, 2);
        counter = 0;
        prevDiff = diff;
      } else {
        counter++;
      }
    }
  }
  buf.push(prevDiff * BLOCKS + counter, 2);

  if (prevDiff === 8 && counter === BLOCKS - 1) changed = false;
  return { changed, buffer: buf };
}

function decodeFieldDiff(prev: InnerField, buffer: Buffer): { field: InnerField; changed: boolean } {
  const field = prev.copy();
  let idx = 0;
  let changed = true;

  while (idx < BLOCKS) {
    const diffBlock = buffer.poll(2);
    const diff = Math.floor(diffBlock / BLOCKS);
    const count = diffBlock % BLOCKS;

    if (diff === 8 && count === BLOCKS - 1) changed = false;

    for (let i = 0; i <= count; i++) {
      const x = idx % W;
      const y = H - 1 - Math.floor(idx / W);
      field.addNumber(x, y, diff - 8);
      idx++;
    }
  }
  return { field, changed };
}

// ============================================================
// エンコード (メイン)
// ============================================================
export function encode(pages: { field?: Field; operation?: Operation; comment?: string; flags?: any }[]): string {
  const buf = new Buffer();
  let prevField = new InnerField();
  let prevComment = '';
  let prevQuiz: Quiz | undefined;
  let repeatStartIdx = -1;

  for (let i = 0; i < pages.length; i++) {
    const page = pages[i];
    const flags = page.flags || {};
    const currentField = page.field ? (page.field as any).field : prevField.copy();

    // フィールド差分
    const diff = encodeFieldDiff(prevField, currentField);
    if (diff.changed) {
      buf.merge(diff.buffer);
      repeatStartIdx = -1;
    } else if (repeatStartIdx < 0 || buf.get(repeatStartIdx) === Buffer.tableLength - 1) {
      buf.merge(diff.buffer);
      buf.push(0, 1);
      repeatStartIdx = buf.length - 1;
    } else if (buf.get(repeatStartIdx) < Buffer.tableLength - 1) {
      buf.set(repeatStartIdx, buf.get(repeatStartIdx) + 1);
    }

    // コメント処理
    let commentText: string | undefined;
    if (page.comment !== undefined) {
      if (page.comment.startsWith('#Q=')) {
        const quiz = new Quiz(page.comment);
        if (prevQuiz && prevQuiz.format().toString() === page.comment) {
          commentText = undefined;
        } else {
          commentText = page.comment;
          prevComment = commentText;
          prevQuiz = quiz;
        }
      } else {
        if (prevQuiz && prevQuiz.format().toString() === page.comment) {
          commentText = undefined;
          prevComment = page.comment;
          prevQuiz = undefined;
        } else {
          commentText = prevComment !== page.comment ? page.comment : undefined;
          prevComment = commentText || prevComment;
          prevQuiz = undefined;
        }
      }
    } else {
      prevQuiz = undefined;
    }

    // Quizの進行
    if (prevQuiz && prevQuiz.canOperate() && flags.lock && page.operation) {
      const piece = parsePiece(page.operation.type);
      if (isMinoPiece(piece)) {
        try {
          const op = prevQuiz.nextIfEnd().getOperation(piece);
          prevQuiz = prevQuiz.nextIfEnd().operate(op);
        } catch (e) {
          prevQuiz = prevQuiz.format();
        }
      } else {
        prevQuiz = prevQuiz.format();
      }
    }

    // アクション
    const piece = page.operation ? {
      type: parsePiece(page.operation.type),
      rotation: parseRotation(page.operation.rotation),
      x: page.operation.x,
      y: page.operation.y,
    } : { type: Piece.Empty, rotation: Rotation.Reverse, x: 0, y: 22 };

    const action: Action = {
      piece,
      rise: !!flags.rise,
      mirror: !!flags.mirror,
      colorize: !!flags.colorize || i === 0,
      lock: flags.lock !== false,
      comment: commentText !== undefined,
    };
    buf.push(encodeAction(action), 3);

    // コメントデータ
    if (commentText !== undefined) {
      const encoded = encodeComment(commentText);
      buf.push(Math.min(commentText.length, 4095), 2);
      for (const v of encoded) buf.push(v, 5);
    }

    // 地形更新
    if (action.lock) {
      if (isMinoPiece(action.piece.type)) {
        currentField.fillAll(
          getBlockXYs(action.piece.type, action.piece.rotation, action.piece.x, action.piece.y),
          action.piece.type,
        );
      }
      currentField.clearLine();
      if (action.rise) currentField.riseGarbage();
      if (action.mirror) currentField.mirror();
    }

    prevField = currentField;
  }

  // ? 挿入
  const data = buf.toString();
  if (data.length < 41) return data;
  const head = data.substr(0, 42);
  const tail = data.substr(42);
  const parts = tail.match(/.{1,47}/g) || [];
  return head + '?' + parts.join('?');
}

// ============================================================
// デコード (メイン)
// ============================================================
export function decode(fumen: string): any[] {
  // バージョン抽出
  let data = fumen;
  const paramIdx = data.indexOf('&');
  if (paramIdx >= 0) data = data.substring(0, paramIdx);

  const match = data.match(/[vmd]115@/);
  if (!match || match.index === undefined) throw new Error('Unsupported fumen version');
  data = data.substr(match.index + 5).replace(/[?\s]+/g, '');

  const buf = new Buffer(data);
  let prevField = new InnerField();
  let repeatCount = -1;
  let pages: any[] = [];
  let pageIndex = 0;
  let fieldRefIdx = 0;
  let commentRefIdx = 0;
  let lastComment = '';
  let quiz: Quiz | undefined;

  while (!buf.isEmpty()) {
    // フィールド
    let fieldObj: { field: InnerField; changed: boolean };
    if (repeatCount > 0) {
      fieldObj = { field: prevField, changed: false };
      repeatCount--;
    } else {
      fieldObj = decodeFieldDiff(prevField.copy(), buf);
      if (!fieldObj.changed) {
        repeatCount = buf.poll(1);
      }
    }

    // アクション
    const action = decodeAction(buf.poll(3));

    // コメント
    let comment: string;
    if (action.comment) {
      const values: number[] = [];
      const len = buf.poll(2);
      for (let i = 0; i < Math.ceil(len / 4); i++) {
        values.push(buf.poll(5));
      }
      comment = decodeComment(values).slice(0, len);
      lastComment = comment;
      commentRefIdx = pageIndex;
      if (Quiz.isQuizComment(comment)) {
        try { quiz = new Quiz(comment); } catch { quiz = undefined; }
      } else {
        quiz = undefined;
      }
    } else if (pageIndex === 0) {
      comment = '';
    } else {
      comment = quiz ? quiz.format().toString() : (lastComment || '');
    }

    // Quiz進行
    if (quiz && quiz.canOperate() && action.lock && isMinoPiece(action.piece.type)) {
      try {
        const op = quiz.nextIfEnd().getOperation(action.piece.type);
        quiz = quiz.nextIfEnd().operate(op);
      } catch { quiz = quiz.format(); }
    }

    // ページ保存
    pages.push({
      index: pageIndex,
      field: fieldObj.field,
      operation: action.piece.type !== Piece.Empty ? {
        type: parsePieceName(action.piece.type),
        rotation: parseRotationName(action.piece.rotation),
        x: action.piece.x,
        y: action.piece.y,
      } : undefined,
      comment: comment,
      flags: {
        lock: action.lock,
        mirror: action.mirror,
        colorize: action.colorize,
        rise: action.rise,
        quiz: !!quiz,
      },
      refs: {
        field: fieldObj.changed || pageIndex === 0 ? undefined : fieldRefIdx,
        comment: action.comment ? undefined : commentRefIdx,
      },
    });

    if (fieldObj.changed || pageIndex === 0) fieldRefIdx = pageIndex;
    pageIndex++;

    // 地形更新
    if (action.lock) {
      if (isMinoPiece(action.piece.type)) {
        fieldObj.field.fillAll(
          getBlockXYs(action.piece.type, action.piece.rotation, action.piece.x, action.piece.y),
          action.piece.type,
        );
      }
      fieldObj.field.clearLine();
      if (action.rise) fieldObj.field.riseGarbage();
      if (action.mirror) fieldObj.field.mirror();
    }

    prevField = fieldObj.field;
  }

  return pages;
}

// ============================================================
// エクスポート
// ============================================================
export { Field as Field, Mino as Mino, Operation as Operation };