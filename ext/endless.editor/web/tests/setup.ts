/*
 * Copyright (c) 2026 dev4fun. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.0,
 * as published by the Free Software Foundation.
 *
 * This program is designed to work with certain software (including
 * but not limited to OpenSSL) that is licensed under separate terms, as
 * designated in a particular file or component or in included license
 * documentation.  The authors of MySQL hereby grant you an additional
 * permission to link the program and your derivative works with the
 * separately licensed software that they have either included with
 * the program or referenced in the documentation.
 * This program is distributed in the hope that it will be useful,  but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License, version 2.0, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

// jsdom lacks the browser APIs the canvas chart relies on; provide inert stand-ins.
if (typeof window !== 'undefined') {
  if (!window.matchMedia) {
    window.matchMedia = (query: string) => ({
      matches: false, media: query, onchange: null, addListener: () => undefined, removeListener: () => undefined,
      addEventListener: () => undefined, removeEventListener: () => undefined, dispatchEvent: () => false,
    } as MediaQueryList);
  }
  if (!('ResizeObserver' in window)) {
    (window as unknown as { ResizeObserver: unknown }).ResizeObserver = class { observe() { /* noop */ } unobserve() { /* noop */ } disconnect() { /* noop */ } };
  }
  const canvas = window.HTMLCanvasElement.prototype as HTMLCanvasElement & { getContext: unknown };
  const context = new Proxy({}, { get: (target, property) => property === 'measureText' ? () => ({ width: 10 }) : property === 'canvas' ? target : typeof property === 'string' && /^(get|create|is)/.test(property) ? () => ({}) : () => undefined });
  canvas.getContext = () => context as unknown as CanvasRenderingContext2D;
  window.HTMLCanvasElement.prototype.toDataURL = () => 'data:image/png;base64,';
  if (!('Path2D' in window)) {
    (window as unknown as { Path2D: unknown }).Path2D = class { moveTo() { /* noop */ } lineTo() { /* noop */ } rect() { /* noop */ } arc() { /* noop */ } closePath() { /* noop */ } addPath() { /* noop */ } bezierCurveTo() { /* noop */ } quadraticCurveTo() { /* noop */ } };
    (globalThis as unknown as { Path2D: unknown }).Path2D = (window as unknown as { Path2D: unknown }).Path2D;
  }
}
