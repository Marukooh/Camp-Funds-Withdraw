#!/usr/bin/env python3
"""Generates the banner (1600x500) and the Nexus thumbnail (1280x720) as SVG, then PNG via rsvg-convert.
Original artwork: no game assets. Font: Noto Serif Display (SIL Open Font License)."""
import math, subprocess, sys
from pathlib import Path

OUT = Path(__file__).resolve().parent
GOLD1, GOLD2, GOLD3 = "#f6dc9a", "#d4a64a", "#8a6420"
SILVER1, SILVER2, SILVER3 = "#f4f6f8", "#b9c0c8", "#5f6873"

def defs():
    return f'''<defs>
  <radialGradient id="bg" cx="50%" cy="45%" r="75%">
    <stop offset="0" stop-color="#3a1714"/><stop offset="0.55" stop-color="#1d0c0b"/><stop offset="1" stop-color="#080404"/>
  </radialGradient>
  <linearGradient id="gold" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="{GOLD1}"/><stop offset="0.55" stop-color="{GOLD2}"/><stop offset="1" stop-color="{GOLD3}"/>
  </linearGradient>
  <linearGradient id="silver" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="{SILVER1}"/><stop offset="0.55" stop-color="{SILVER2}"/><stop offset="1" stop-color="{SILVER3}"/>
  </linearGradient>
  <linearGradient id="wood" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#6b3a22"/><stop offset="1" stop-color="#2e160c"/>
  </linearGradient>
  <filter id="grain" x="0" y="0" width="100%" height="100%">
    <feTurbulence type="fractalNoise" baseFrequency="0.85" numOctaves="2" seed="7" result="n"/>
    <feColorMatrix type="matrix" values="0 0 0 0 0  0 0 0 0 0  0 0 0 0 0  0 0 0 0.10 0"/>
  </filter>
  <filter id="glow" x="-30%" y="-30%" width="160%" height="160%">
    <feGaussianBlur stdDeviation="6" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge>
  </filter>
  <filter id="shadow" x="-20%" y="-20%" width="140%" height="160%">
    <feDropShadow dx="0" dy="6" stdDeviation="6" flood-color="#000" flood-opacity="0.7"/>
  </filter>
</defs>'''

def sigil(cx, cy, r, op=0.16):
    s = [f'<g opacity="{op}" stroke="{GOLD2}" fill="none">',
         f'<circle cx="{cx}" cy="{cy}" r="{r}" stroke-width="2"/>',
         f'<circle cx="{cx}" cy="{cy}" r="{r*0.86}" stroke-width="1"/>',
         f'<circle cx="{cx}" cy="{cy}" r="{r*0.55}" stroke-width="1.5" stroke-dasharray="2 6"/>']
    for i in range(72):
        a = i * math.tau / 72
        r1, r2 = r * 0.86, r * (0.93 if i % 6 else 1.0)
        s.append(f'<line x1="{cx+r1*math.cos(a):.1f}" y1="{cy+r1*math.sin(a):.1f}" '
                 f'x2="{cx+r2*math.cos(a):.1f}" y2="{cy+r2*math.sin(a):.1f}" stroke-width="1.2"/>')
    for k in range(4):
        a = k * math.tau / 4 + math.pi / 4
        s.append(f'<line x1="{cx+r*0.55*math.cos(a):.1f}" y1="{cy+r*0.55*math.sin(a):.1f}" '
                 f'x2="{cx+r*1.15*math.cos(a):.1f}" y2="{cy+r*1.15*math.sin(a):.1f}" stroke-width="1"/>')
    s.append('</g>')
    return '\n'.join(s)

def coin(cx, cy, r, grad, edge, mark):
    return (f'<g filter="url(#shadow)"><ellipse cx="{cx}" cy="{cy+r*0.16}" rx="{r}" ry="{r*0.92}" fill="{edge}"/>'
            f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="url(#{grad})" stroke="{edge}" stroke-width="2"/>'
            f'<circle cx="{cx}" cy="{cy}" r="{r*0.78}" fill="none" stroke="{edge}" stroke-width="1.5" opacity="0.7"/>'
            f'<text x="{cx}" y="{cy+r*0.30}" text-anchor="middle" font-family="Noto Serif Display" font-weight="bold" '
            f'font-size="{r*0.9:.0f}" fill="{edge}" opacity="0.85">{mark}</text></g>')

def chest(x, y, w, h):
    lid = h * 0.42
    return f'''<g filter="url(#shadow)">
  <path d="M{x},{y+lid} Q{x},{y} {x+w/2},{y} Q{x+w},{y} {x+w},{y+lid} Z" fill="url(#wood)" stroke="{GOLD3}" stroke-width="3"/>
  <rect x="{x}" y="{y+lid}" width="{w}" height="{h-lid}" rx="6" fill="url(#wood)" stroke="{GOLD3}" stroke-width="3"/>
  <rect x="{x+w*0.12}" y="{y+lid*0.25}" width="{w*0.07}" height="{h*0.9}" fill="url(#gold)" opacity="0.9"/>
  <rect x="{x+w*0.81}" y="{y+lid*0.25}" width="{w*0.07}" height="{h*0.9}" fill="url(#gold)" opacity="0.9"/>
  <rect x="{x+w*0.42}" y="{y+lid*0.8}" width="{w*0.16}" height="{h*0.30}" rx="4" fill="url(#gold)" stroke="{GOLD3}" stroke-width="2"/>
  <circle cx="{x+w/2}" cy="{y+lid*0.8+h*0.13}" r="{h*0.035}" fill="#1a0c06"/>
</g>'''

def arrow(x1, y, x2):
    return (f'<g filter="url(#glow)" stroke="{GOLD2}" fill="none" stroke-width="5" stroke-linecap="round">'
            f'<path d="M{x1},{y} L{x2},{y}"/><path d="M{x2-22},{y-16} L{x2},{y} L{x2-22},{y+16}"/></g>')

def emblem(cx, cy, s):
    """Chest of camp funds -> silver coins."""
    parts = [chest(cx - 175*s, cy - 55*s, 150*s, 120*s)]
    parts.append(coin(cx - 120*s, cy - 72*s, 24*s, "gold", GOLD3, "✦"))
    parts.append(coin(cx - 80*s, cy - 84*s, 20*s, "gold", GOLD3, "✦"))
    parts.append(arrow(cx - 10*s, cy + 5*s, cx + 70*s))
    parts.append(coin(cx + 140*s, cy + 30*s, 44*s, "silver", SILVER3, "S"))
    parts.append(coin(cx + 175*s, cy - 18*s, 40*s, "silver", SILVER3, "S"))
    parts.append(coin(cx + 118*s, cy - 30*s, 36*s, "silver", SILVER3, "S"))
    return '\n'.join(parts)

def frame(w, h, m=18):
    return (f'<rect x="{m}" y="{m}" width="{w-2*m}" height="{h-2*m}" fill="none" stroke="{GOLD2}" stroke-width="2" opacity="0.55"/>'
            f'<rect x="{m+8}" y="{m+8}" width="{w-2*m-16}" height="{h-2*m-16}" fill="none" stroke="{GOLD2}" stroke-width="1" opacity="0.3"/>')

def title(x, y, size, anchor="middle"):
    return f'''<g filter="url(#shadow)" font-family="Noto Serif Display" text-anchor="{anchor}">
  <text x="{x}" y="{y}" font-size="{size}" font-weight="bold" letter-spacing="{size*0.08:.1f}" fill="url(#gold)">WITHDRAW CAMP FUNDS</text>
</g>'''

def banner():
    W, H = 1600, 500
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">
{defs()}
<rect width="{W}" height="{H}" fill="url(#bg)"/>
<rect width="{W}" height="{H}" filter="url(#grain)"/>
{sigil(330, 250, 210)}
{sigil(1330, 250, 170, 0.08)}
{frame(W, H)}
{emblem(330, 255, 1.05)}
{title(1060, 200, 60)}
<line x1="740" y1="236" x2="1380" y2="236" stroke="{GOLD2}" stroke-width="1.5" opacity="0.6"/>
<circle cx="1060" cy="236" r="5" fill="{GOLD2}"/>
<text x="1060" y="290" text-anchor="middle" font-family="Noto Serif Display" font-size="34" fill="#efe3c8">Camp Funds  ➜  Silver</text>
<text x="1060" y="338" text-anchor="middle" font-family="Noto Serif Display" font-style="italic" font-size="24" fill="#c9b89a">A new option in Carl's menu at the Greymane Camp</text>
<text x="1060" y="416" text-anchor="middle" font-family="Noto Serif Display" font-size="20" letter-spacing="6" fill="{GOLD2}" opacity="0.85">CRIMSON DESERT MOD</text>
</svg>'''

def thumbnail():
    W, H = 1280, 720
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">
{defs()}
<rect width="{W}" height="{H}" fill="url(#bg)"/>
<rect width="{W}" height="{H}" filter="url(#grain)"/>
{sigil(640, 300, 250)}
{frame(W, H)}
{emblem(640, 290, 1.25)}
{title(640, 520, 66)}
<line x1="300" y1="550" x2="980" y2="550" stroke="{GOLD2}" stroke-width="1.5" opacity="0.6"/>
<text x="640" y="600" text-anchor="middle" font-family="Noto Serif Display" font-size="34" fill="#efe3c8">Camp Funds  ➜  Silver</text>
<text x="640" y="650" text-anchor="middle" font-family="Noto Serif Display" font-size="20" letter-spacing="6" fill="{GOLD2}" opacity="0.85">CRIMSON DESERT MOD</text>
</svg>'''

for name, svg in (("banner", banner()), ("thumbnail", thumbnail())):
    (OUT / f"{name}.svg").write_text(svg)
    subprocess.run(["rsvg-convert", "-o", str(OUT / f"{name}.png"), str(OUT / f"{name}.svg")], check=True)
    print(OUT / f"{name}.png")
