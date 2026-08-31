# bentogame.py — a tiny, beginner-friendly game framework for the
# BENTO Game Console, written in pure MicroPython on top of the existing
# `ui` (graphics) and `joystick` (input) modules. No firmware build needed.
#
# Design goal: a 2nd-year student who is weak at C can make a real,
# on-screen, joystick-controlled game by learning only ~6 ideas:
#   start(), Box, Text, keys(), hit(), run()
#
# The SAME concepts (sprite, move, input, collision, loop) map 1:1 onto the
# C game SDK later in the course — so Python is the on-ramp, not a dead end.
#
# This file is ONE framework for BOTH kits:
#   - Eva/AI Game Console: joystick, menu/game_over/replay/pause, scenery, sfx
#   - TESAIoT Dev Kit (QWA309 base board): game.pot/button/matrix/link extras
# Every hardware-specific call degrades gracefully (neutral value, no crash)
# on boards without that hardware — the same game file runs everywhere.

import ui
import joystick
import time

# Display size of the BENTO console canvas (matches ui.screen default).
WIDTH = 792
HEIGHT = 398

# ---------------------------------------------------------------------------
# วัดความกว้างข้อความเพื่อจัดกลางให้ตรงจริง
#
# ก่อนหน้านี้ทุกจุดที่จัดกลางใช้ `WIDTH // 2 - len(s) * 4` ซึ่งเท่ากับสมมติว่า
# ตัวอักษรทุกตัวกว้าง 8 px เท่ากันหมด แต่ฟอนต์จริงของ Label คือ Montserrat 20
# (`UI_DEF_LABEL_FONT_SIZE = 20` ใน ui_widget_defaults.h:35 แล้ว apply_font_size()
# ใน ui_widget_mgr.c:230 เลือก lv_font_montserrat_20) ซึ่งเป็นฟอนต์ proportional
# เว้นวรรคกว้าง 5 px ตัวพิมพ์เล็กราว 12 px ตัวพิมพ์ใหญ่ถึง 15 px
#
# ผลคือทุกข้อความที่ "จัดกลาง" เลื่อนไปทางขวาเสมอ และเลื่อนมากขึ้นตามความกว้างจริง
# วัดจากภาพหน้าจอบอร์ดจริง: "MAZE PURSUIT AI" เบี้ยวขวา 31 px และบรรทัดล่างของแผง
# GAME OVER ยาวล้นไปติดขอบแผง
#
# ตาราง _ADVW เก็บความกว้างจริงของ ASCII 32..126 (หน่วย px ปัดจาก .adv_w ของ
# lv_font_montserrat_20.c ซึ่งเก็บเป็น 1/16 px) เข้ารหัสเป็นสตริงเดียวโดยบวก 32
# ให้อ่านออกเป็น ASCII ได้ จึงไม่กินหน่วยความจำเท่า list ของ int
#
# ตัวอักษรไทยใช้ฟอนต์ Noto Thai คนละชุด ยังไม่มีตารางให้ จึงประมาณที่ 13 px
# ซึ่งใกล้ค่าเฉลี่ยของ Noto Thai 20 พอให้จัดกลางไม่เพี้ยนจนสังเกตได้
_ADVW = ("%%(.,1.$''(,%(%'-',+-,,,-,%%,,,+5//.0--/0&*.,301.1/,,0.6---'''"
         ",*,,.+.,'..&&,&5.-..(*(.+2++*'&',")


def text_width(s):
    """ความกว้างของข้อความเป็นพิกเซล ใช้จัดกลางแทนการเดาจาก len()"""
    w = 0
    for ch in s:
        c = ord(ch)
        w += ord(_ADVW[c - 32]) - 32 if 32 <= c < 127 else 13
    return w


def center_x(s, box_w=WIDTH, box_x=0):
    """พิกัด x ที่ทำให้ข้อความ s อยู่กลางกรอบกว้าง box_w ซึ่งเริ่มที่ box_x"""
    return box_x + (box_w - text_width(s)) // 2

# A few friendly color names so beginners don't fight hex codes.
RED     = 0xFF5555
GREEN   = 0x50FA7B
BLUE    = 0x61AFEF
YELLOW  = 0xF1FA8C
CYAN    = 0x00E0FF
ORANGE  = 0xFFB86C
WHITE   = 0xFFFFFF
PINK    = 0xFF79C6
BLACK   = 0x101010

# Classic Game Boy 4-tone palette (same values as the on-board C games).
GB_DARKEST  = 0x0F380F
GB_DARK     = 0x306230
GB_LIGHT    = 0x8BAC0F
GB_LIGHTEST = 0x9BBC0F


# --- Sound -------------------------------------------------------------------
# Plays through the on-board C sound engine (bento_sfx) over IPC — no extra
# hardware, no firmware build. Needs sfx-capable firmware; older images stay
# silent so your game still runs.
_HAS_SOUND = hasattr(ui, "sfx")

if _HAS_SOUND:
    # Waveforms for tone()
    SINE, SQUARE, TRIANGLE, SAW = (ui.WAVE_SINE, ui.WAVE_SQUARE,
                                   ui.WAVE_TRIANGLE, ui.WAVE_SAW)
    # Friendly names -> the real firmware ids (always in sync with the C games).
    SFX = {
        "move": ui.SFX_UI_MOVE, "select": ui.SFX_UI_SELECT, "back": ui.SFX_UI_BACK,
        "deny": ui.SFX_UI_DENY, "start": ui.SFX_UI_START,
        "eat": ui.SFX_SNAKE_EAT, "turn": ui.SFX_SNAKE_TURN, "die": ui.SFX_SNAKE_DIE,
        "flap": ui.SFX_FLAPPY_FLAP, "point": ui.SFX_FLAPPY_SCORE,
        "wall": ui.SFX_PONG_WALL, "paddle": ui.SFX_PONG_PADDLE,
        "win": ui.SFX_PONG_WIN, "lose": ui.SFX_PONG_LOSE,
        "fire": ui.SFX_SHOOT_FIRE, "hit": ui.SFX_SHOOT_HIT,
        "explode": ui.SFX_SHOOT_EXPLODE, "gameover": ui.SFX_GAME_OVER,
        # the remaining real-game sounds, now reachable by a friendly name too
        "fall": ui.SFX_FLAPPY_DIE, "pong_score": ui.SFX_PONG_SCORE,
        "lose_life": ui.SFX_SHOOT_LOSE_LIFE,
    }
else:
    SINE = SQUARE = TRIANGLE = SAW = 0
    SFX = {}


def sfx(name):
    """Play a built-in sound effect — by name ("eat","die","gameover",...) or number."""
    if not _HAS_SOUND:
        return
    try:
        ui.sfx(SFX.get(name, name) if isinstance(name, str) else int(name))
    except Exception:
        pass


def tone(note, wave=None, velocity=100, ms=150):
    """Play your own note (MIDI note number, 60 = middle C). Invent your own sounds!"""
    if not _HAS_SOUND:
        return
    try:
        ui.tone(int(note), SQUARE if wave is None else wave, int(velocity), int(ms))
    except Exception:
        pass


# --- Real sprites (spike-b) -------------------------------------------------
# game.Sprite("snake_body", x, y) draws the SAME pixel-art the on-board C games
# use (the C side owns the pixels; only the name+position cross IPC). Needs
# sprite-capable firmware; on the desktop sim (or old firmware) it falls back to
# a small GB-green box so your game logic still runs (shapes, not pixel-art).
_HAS_SPRITE = hasattr(ui, "Sprite")

if _HAS_SPRITE:
    SPR = {
        "snake_head_r": ui.SPR_SNAKE_HEAD_R, "snake_head_d": ui.SPR_SNAKE_HEAD_D,
        "snake_head_l": ui.SPR_SNAKE_HEAD_L, "snake_head_u": ui.SPR_SNAKE_HEAD_U,
        "snake_body": ui.SPR_SNAKE_BODY, "snake_food": ui.SPR_SNAKE_FOOD,
        "bird": ui.SPR_FLAPPY_BIRD, "pipe_cap": ui.SPR_FLAPPY_PIPE_CAP,
        "ball": ui.SPR_PONG_BALL, "paddle": ui.SPR_PONG_PADDLE,
        "ship": ui.SPR_SHOOTER_SHIP, "enemy": ui.SPR_SHOOTER_ENEMY,
        "enemy2": ui.SPR_SHOOTER_ENEMY2, "enemy3": ui.SPR_SHOOTER_ENEMY3,
        "boom1": ui.SPR_SHOOTER_BOOM_P1, "boom2": ui.SPR_SHOOTER_BOOM_P2,
        "boom3": ui.SPR_SHOOTER_BOOM_C1, "boom4": ui.SPR_SHOOTER_BOOM_C2,
        "boom5": ui.SPR_SHOOTER_BOOM_F1, "boom6": ui.SPR_SHOOTER_BOOM_F2,
    }
else:
    SPR = {}


def _r(fn, *a, **k):
    """Retry a ui call while CM55 signals transient "not ready" backpressure
    (the same tolerance start() already uses for ui.screen). A heavy scene can
    briefly outrun the CM55 graphics core; retrying instead of raising keeps the
    game from crashing mid-frame."""
    for _ in range(40):
        try:
            return fn(*a, **k)
        except Exception as _e:
            if "not ready" in str(_e):
                time.sleep_ms(15)
                continue
            raise
    return fn(*a, **k)


class Sprite:
    """A real pixel-art sprite from the on-board C games (e.g. "snake_body").
    Same moves as Box; .frame("snake_head_u") swaps which picture it shows."""

    def __init__(self, name, x=0, y=0):
        self.x, self.y, self.name = x, y, name
        if _HAS_SPRITE:
            self._w = _r(ui.Sprite, SPR[name], int(x), int(y))
        else:
            # logic-only fallback so the desktop sim still runs (a 16px box)
            self._w = _r(ui.Panel, x=int(x), y=int(y), w=16, h=16, color=GB_DARK,
                               min=GB_DARK, max=1, value=0)

    def move_to(self, x, y):
        self.x, self.y = x, y
        _r(self._w.pos, int(x), int(y))

    def move(self, delta_x, delta_y):
        self.move_to(self.x + delta_x, self.y + delta_y)

    def frame(self, name):
        """Swap the picture (e.g. turn the snake head). No-op in the sim."""
        self.name = name
        if _HAS_SPRITE:
            _r(self._w.frame, SPR[name])

    def hide(self):
        _r(self._w.hide)

    def show(self):
        _r(self._w.show)

    def center(self):
        return (self.x + 8, self.y + 8)

    def delete(self):
        """ลบสไปรต์นี้ทิ้งถาวร คืนงบ widget ให้จอ"""
        try:
            self._w.delete()
        except Exception:
            pass


def pool(name, count):
    """Make `count` hidden sprites of one kind ONCE (e.g. snake body segments).
    Show/move the ones you need each frame, hide the rest — exactly like the C
    game. Keeps you under the 32-sprite limit and runs fast.
    NEVER create sprites inside your update() loop; size a pool here instead."""
    sprites = [Sprite(name, -100, -100) for _ in range(count)]
    for sprite in sprites:
        sprite.hide()
    return sprites


def start(width=WIDTH, height=HEIGHT):
    """Call once at the top of your game. Clears the screen and wakes the joystick."""
    global WIDTH, HEIGHT
    WIDTH, HEIGHT = width, height
    # Right after a soft-reset (Start = restart) CM33 can reach here before the
    # CM55 graphics core is ready — ui.screen() then raises "CM55 not ready
    # (retry shortly)". Retry for up to ~5s so restart never crashes.
    for _attempt in range(50):
        try:
            ui.screen(width, height)
            break
        except Exception:
            time.sleep_ms(100)
    try:
        joystick.init()
    except Exception:
        pass  # touch-only is fine; joystick is optional


def clear():
    """Erase everything on the screen."""
    _r(ui.clear)


class Box:
    """A colored rectangle you can move around. The simplest 'sprite'.

    แต่งขอบกับมุมได้แบบเดียวกับเกม C (จอวาดขอบให้กล่องเสมออย่างน้อย 1px —
    ค่าปกติเราจึงย้อมขอบเป็นสีเดียวกับตัวกล่อง จะได้มองไม่เห็นขอบ):
      border   = สีขอบ เช่น game.WHITE (ไม่ใส่ = สีเดียวกับกล่อง)
      border_w = ความหนาขอบเป็น px (0 = บางสุดที่จอทำได้ คือ 1px)
      radius   = ความโค้งมุม (1 = เหลี่ยมสุดที่จอทำได้, ใส่มากขึ้น = มนขึ้น)
    เช่นกระสุนขอบขาวมุมมนของเกม C:
        game.Box(x, y, 5, 12, LASER, border=game.WHITE, radius=2)"""

    def __init__(self, x, y, w, h, color=WHITE, border=None, radius=1, border_w=0):
        self.x = x
        self.y = y
        self.w = w
        self.h = h
        edge = color if border is None else border
        if edge == 0:                  # จอถือว่าสีขอบ 0 คือ "ใช้สีตามธีม" — เลี่ยงด้วยดำเกือบสนิท
            edge = 0x101010
        self._panel = _r(ui.Panel, x=int(x), y=int(y), w=int(w), h=int(h), color=color,
                               min=edge, max=max(1, int(radius)),
                               value=int(border_w))

    def move_to(self, x, y):
        """Put the box at an exact position."""
        self.x = x
        self.y = y
        _r(self._panel.pos, int(x), int(y))

    def move(self, delta_x, delta_y):
        """Move the box by some amount, staying on screen."""
        next_x = self.x + delta_x
        next_y = self.y + delta_y
        if next_x < 0:
            next_x = 0
        if next_y < 0:
            next_y = 0
        if next_x > WIDTH - self.w:
            next_x = WIDTH - self.w
        if next_y > HEIGHT - self.h:
            next_y = HEIGHT - self.h
        self.move_to(next_x, next_y)

    def resize(self, w, h):
        """Change the box's width/height (e.g. Flappy pipes)."""
        self.w, self.h = w, h
        _r(self._panel.size, int(w), int(h))

    def set_color(self, color):
        _r(self._panel.color, color)

    def hide(self):
        _r(self._panel.hide)

    def show(self):
        _r(self._panel.show)

    def center(self):
        """Return (cx, cy) — the middle point of the box."""
        return (self.x + self.w / 2, self.y + self.h / 2)

    def delete(self):
        """ลบกล่องนี้ทิ้งถาวร คืนงบ widget ให้จอ (ใช้กับฉากที่เลิกใช้แล้ว)"""
        try:
            self._panel.delete()
        except Exception:
            pass


class Text:
    """A line of text on screen (e.g. the score)."""

    def __init__(self, message, x, y, color=WHITE):
        self._label = _r(ui.Label, str(message), x=int(x), y=int(y), color=color)

    def set(self, message):
        _r(self._label.text, str(message))

    def delete(self):
        """ลบป้ายนี้ทิ้งถาวร คืนงบ widget ให้จอ"""
        try:
            self._label.delete()
        except Exception:
            pass


class Keys:
    """A friendly snapshot of the controller this frame.

    .left .right .up .down  -> True/False  (D-pad OR left stick)
    .a .b .x .y             -> face buttons
    .start .back            -> system buttons
    """

    def __init__(self, raw):
        self.raw = raw or {}
        threshold = 60  # stick threshold around center (128)
        stick_x = self.raw.get("left_x", 128)
        stick_y = self.raw.get("left_y", 127)
        self.left  = bool(self.raw.get("dpad_left"))  or (stick_x < 128 - threshold)
        self.right = bool(self.raw.get("dpad_right")) or (stick_x > 128 + threshold)
        self.up    = bool(self.raw.get("dpad_up"))    or (stick_y < 127 - threshold)
        self.down  = bool(self.raw.get("dpad_down"))  or (stick_y > 127 + threshold)
        self.a = bool(self.raw.get("a"))
        self.b = bool(self.raw.get("b"))
        self.x = bool(self.raw.get("x"))
        self.y = bool(self.raw.get("y"))
        self.start = bool(self.raw.get("start"))
        self.back  = bool(self.raw.get("back"))


# (ส่วนนี้ของระบบทดสอบอัตโนมัติของคอร์ส — นิสิตข้ามไปได้เลย)
_script_src = None     # iterator ของ "ปุ่มที่ถูกกด" ทีละเฟรม (dict แบบ joystick.read)
_script_done = False   # True เมื่อสคริปต์หมด — ทุก loop ที่รอปุ่มจะออกให้เอง


def _use_script(frames):
    global _script_src, _script_done
    _script_src = iter(frames) if frames is not None else None
    _script_done = False


def _script_next():
    global _script_src, _script_done
    if _script_src is None:
        return None
    try:
        item = next(_script_src)
        return item if isinstance(item, dict) else {}
    except StopIteration:
        _script_src = None
        _script_done = True
        return None


def keys():
    """Read the controller right now. Returns a Keys object."""
    if _script_src is not None:
        item = _script_next()
        if item is not None:
            return Keys(item)
    try:
        return Keys(joystick.read())
    except Exception:
        return Keys({})


# จำสถานะปุ่มของเฟรมก่อน ให้ pressed_once() จับ "จังหวะกด" ได้
_edge_prev = {}


def pressed_once(key, snapshot=None):
    """True เฉพาะจังหวะแรกที่ปุ่มเพิ่งถูกกด (กดค้างไม่นับ) — แบบเดียวกับเกม C
    ใช้กับปุ่มยิง/กระพือปีก จะได้ไม่กลายเป็นกดค้างแล้วยิงรัว:

        if game.pressed_once("a"):
            ยิงกระสุน()

    ถ้าใน update() อ่าน keys() ไว้แล้ว ส่งเข้ามาด้วยจะได้ไม่อ่านซ้ำ:
        k = game.keys()
        if game.pressed_once("a", k): ...
    ชื่อปุ่มที่ใช้ได้: "a" "b" "x" "y" "up" "down" "left" "right" "start" "back"
    """
    k = snapshot if snapshot is not None else keys()
    now = bool(getattr(k, key, False))
    fired = now and not _edge_prev.get(key, False)
    _edge_prev[key] = now
    return fired


# --- Sensors & IoT (Option C) ------------------------------------------------
# Tilt control reads the on-board BMI270; online ranking publishes over MQTT.
# Both are forgiving: if the hardware/network isn't there they no-op so your
# game still runs (e.g. in the desktop sim).
_tilt_inited = False


def tilt(deadzone=0.18):
    """Tilt the board to steer — reads the BMI270 and returns a Keys object
    (.left/.right/.up/.down). First call wakes the sensor. All-False if missing."""
    global _tilt_inited
    steering = Keys({})
    try:
        import sensors
        if not _tilt_inited:
            sensors.init()
            _tilt_inited = True
        accel_x, accel_y, accel_z = sensors.bmi270.acceleration()
    except Exception:
        return steering
    steering.right = accel_x > deadzone
    steering.left = accel_x < -deadzone
    steering.down = accel_y > deadzone
    steering.up = accel_y < -deadzone
    return steering


_net_ready = False


def connect(ssid, password, broker, port=1883):
    """Connect WiFi + MQTT once (for the online ranking). Call near game start;
    safe to call again. Returns True when ready, False if it can't connect."""
    global _net_ready
    try:
        import wifi, mqtt
        if not wifi.is_connected():
            wifi.connect(ssid, password)
        if not _net_ready:
            mqtt.connect(broker, port=port)
            _net_ready = True
        return True
    except Exception:
        return False


def post_score(score, team="team1", game="snake"):
    """Publish a score to the class ranking over MQTT (call connect() first).
    Topic: game/<game>/<team>/score. Returns True if it was sent."""
    try:
        import mqtt
        if not mqtt.is_connected():
            return False
        topic = "game/%s/%s/score" % (game, team)
        mqtt.publish(topic, '{"team":"%s","game":"%s","score":%d}' % (team, game, score))
        return True
    except Exception:
        return False


def send(state, topic="game/multiplayer/state"):
    """Publish your game state to other players over MQTT (call connect() first).
    `state` is any string. No-op (returns False) when the network is absent."""
    try:
        import mqtt
        if not mqtt.is_connected():
            return False
        mqtt.publish(topic, state)
        return True
    except Exception:
        return False


def recv():
    """Return the latest message from other players, or None if nothing arrived.
    Safe to call every frame; no-op (returns None) when the network is absent."""
    try:
        import mqtt
        if not mqtt.is_connected():
            return None
        return mqtt.get_message()
    except Exception:
        return None


def hit(a, b):
    """True if Box a overlaps Box b (axis-aligned bounding-box test)."""
    return (a.x < b.x + b.w and a.x + a.w > b.x and
            a.y < b.y + b.h and a.y + a.h > b.y)


# --- Console controls (TESAIoT Game Console / QWA309 base board) --------------
# Four paddle knobs (VR1-4), two arcade buttons (SW9/SW10), a touch slider and
# a 16x8 RGB matrix. All forgiving: on boards without this hardware every call
# returns a neutral value so the same game runs everywhere.
try:
    import pots as _pots
    _HAS_POTS = True
except ImportError:
    _HAS_POTS = False

try:
    import buttons as _buttons
    _HAS_BUTTONS = True
except ImportError:
    _HAS_BUTTONS = False

try:
    import rgbmatrix as _rgb
    _HAS_MATRIX = True
except ImportError:
    _HAS_MATRIX = False

# EMA + hysteresis state per pot so paddles don't shimmer when untouched.
_pot_ema = [None, None, None, None]


def pot(n=0):
    """Read paddle knob n (0..3 = VR1..VR4) as 0.0..1.0 — absolute position,
    smoothed for game feel. This IS the classic Pong control:
        paddle_y = game.pot(0) * (game.HEIGHT - paddle_h)
    Returns -1.0 when the console has no pots (use the joystick fallback)."""
    if not _HAS_POTS:
        return -1.0
    try:
        raw = _pots.read(n)
    except Exception:
        return -1.0
    prev = _pot_ema[n]
    if prev is None:
        _pot_ema[n] = raw
    elif abs(raw - prev) <= 4:
        raw = prev                      # hysteresis: ignore 1-2 LSB jitter
    else:
        raw = (raw + prev) // 2         # EMA alpha 0.5
        _pot_ema[n] = raw
    return raw / 4095


def pot_raw(n=0):
    """Read paddle knob n unfiltered (0..4095) — for the 'noise lab' lesson.
    Compare with game.pot(n) to SEE what the filter does. -1 if absent."""
    if not _HAS_POTS:
        return -1
    try:
        return _pots.read(n)
    except Exception:
        return -1


_btn_last = [False, False]


def button(n=0):
    """Arcade button n held down right now? (0 = SW9, 1 = SW10, debounced).
    False when the console has no buttons."""
    if not _HAS_BUTTONS:
        return False
    try:
        return bool(_buttons.pressed(n))
    except Exception:
        return False


def button_pressed(n=0):
    """True ONCE per physical press (edge-triggered) — the right test for
    flap/fire so holding the button doesn't autofire."""
    if n < 0 or n >= len(_btn_last):
        return False
    now = button(n)
    was = _btn_last[n]
    _btn_last[n] = now
    return now and not was


def slider():
    """Touch-slider position 0..100, or None when absent/asleep."""
    try:
        import sensors
        return sensors.capsense.slider()
    except Exception:
        return None


def touch(n=0):
    """CapSense touch button n (0 or 1) held? False when absent."""
    try:
        import sensors
        btn0, btn1 = sensors.capsense.buttons()
        return bool(btn1 if n else btn0)
    except Exception:
        return False


class _Matrix:
    """The 16x8 RGB matrix as a scoreboard/HUD — or as the whole game.
    Colors are 0..7: OFF RED GREEN YELLOW BLUE PURPLE CYAN WHITE.
    Every call is a no-op on consoles without the matrix."""

    # color numbers, mirrored so students can write game.matrix.RED
    OFF, RED, GREEN, YELLOW, BLUE, PURPLE, CYAN, WHITE = range(8)
    WIDTH, HEIGHT = 16, 8

    def clear(self):
        if _HAS_MATRIX:
            try: _rgb.clear()
            except Exception: pass

    def fill(self, color):
        if _HAS_MATRIX:
            try: _rgb.fill(color)
            except Exception: pass

    def pixel(self, x, y, color):
        if _HAS_MATRIX:
            try: _rgb.pixel(x, y, color)
            except Exception: pass

    def blit(self, buf):
        """Draw a whole 128-pixel frame in ONE call. `buf` = 64-byte
        bytearray: byte = buf[y*8 + x//2], even x = low nibble, color 0..7.
        Use frame(), set() and blit() together for matrix games."""
        if _HAS_MATRIX:
            try: _rgb.blit(buf)
            except Exception: pass

    def frame(self):
        """A blank 64-byte frame buffer for blit()."""
        return bytearray(64)

    def set(self, buf, x, y, color):
        """Set pixel (x,y) in a frame buffer (does not draw — call blit())."""
        if 0 <= x < 16 and 0 <= y < 8:
            i = y * 8 + (x >> 1)
            if x & 1:
                buf[i] = (buf[i] & 0x0F) | ((color & 0x0F) << 4)
            else:
                buf[i] = (buf[i] & 0xF0) | (color & 0x0F)

    def score(self, value, color=1):
        """Show a number (0..9999) on the matrix — rendered by the firmware."""
        if _HAS_MATRIX:
            try: _rgb.score(value, color)
            except Exception: pass

    def bar(self, value, max=5, color=2):
        """Life/energy bar on the bottom row: lit = value/max of 16 pixels."""
        if _HAS_MATRIX:
            try: _rgb.bar(value, max, color)
            except Exception: pass

    def scroll(self, text, color=7, ms=80):
        """Marquee text (A-Z, 0-9). Send it ONCE — the firmware keeps it
        scrolling even while your game loop is busy. scroll("") stops it."""
        if _HAS_MATRIX:
            try: _rgb.scroll(text, color, ms)
            except Exception: pass


matrix = _Matrix()


# --- CAN link: console-vs-console multiplayer ---------------------------------
# Two consoles + one twisted pair = wired multiplayer with < 1 ms latency and
# no broker. game.link.send/recv deliberately rhyme with the MQTT game.send/
# game.recv pair (s13): same game code, different transport.
#
# Frames (classic CAN 2.0A @ 500 kbps, 8-byte payloads). TX ids carry the
# device nonce's low bit (two nodes must never transmit the SAME id at once):
#   0x7E0/0x7E1  HELLO      'H', ver, nonce u32          (pairing/role election)
#   0x101        INPUT      seq u8, buttons u8, pot u16  (guest, 60 Hz)
#   0x200        STATE      x u16, y u16, score_l u8, score_r u8,
#                           flags u8 (= host paddle 0..255), seq u8  (host, 60 Hz)
#   0x300        EVENT      sfx u8, event u8             (host, on event; latched)
#   0x7F0/0x7F1  HEARTBEAT  seq u8                       (2 Hz)
# Role election: lower nonce = host (owns the physics; the guest renders STATE
# and sends INPUT). A third console that only recv()s = free spectator mode.
class _Link:
    def __init__(self):
        self._role = None
        self._nonce = None
        self._seq = 0
        self._last_rx = None
        self._last_hb = 0
        self._tx = bytearray(8)      # preallocated: no allocation per frame
        self._hello = None
        self._hello_id = 0x7E0
        self._event = None           # one-slot latch: EVENTs survive a STATE in the same drain

    def _make_nonce(self):
        try:
            import machine
            uid = machine.unique_id()
            n = 0
            for b in uid:
                n = ((n << 5) + n + b) & 0x7FFFFFFF   # djb2-ish, per-device
            return n if n else 1
        except Exception:
            import time
            return (time.ticks_us() & 0x7FFFFFFF) or 1

    def start(self, player=None, wait_ms=1500):
        """Join the wire. Returns "host", "guest", or None (no peer yet).
        Call once near game start; safe to call again."""
        try:
            import can, ustruct, time
            can.init()
            self._nonce = self._make_nonce()
            # MicroPython struct has no 'c' typecode — pack 'H' as a byte.
            hello = ustruct.pack("<BBI2s", ord("H"), 1, self._nonce, b"\x00\x00")
            self._hello = hello
            # Classic CAN forbids two nodes transmitting the SAME id at once
            # (equal-id frames can't arbitrate -> error frames). Split the
            # HELLO id by the device nonce's low bit so the two consoles
            # never collide; listen on both.
            self._hello_id = 0x7E0 | (self._nonce & 1)
            deadline = time.ticks_add(time.ticks_ms(), wait_ms)
            peer_nonce = None
            while time.ticks_diff(deadline, time.ticks_ms()) > 0:
                can.send(self._hello_id, hello)
                frame = can.recv()
                while frame is not None:
                    fid, data = frame
                    if (fid & 0x7FE) == 0x7E0 and len(data) >= 6 and data[0] == 0x48:
                        n = ustruct.unpack_from("<I", data, 2)[0]
                        if n != self._nonce:
                            peer_nonce = n
                    frame = can.recv()
                if peer_nonce is not None:
                    break
                time.sleep_ms(50)
            if peer_nonce is None:
                self._role = None
                return None
            self._role = "host" if self._nonce < peer_nonce else "guest"
            self._last_rx = time.ticks_ms()
            # answer a few extra HELLOs so the slower console also pairs
            for _ in range(3):
                can.send(self._hello_id, hello)
                time.sleep_ms(20)
            return self._role
        except Exception:
            self._role = None
            return None

    def send(self, x=0, y=0, score_l=0, score_r=0, flags=0, buttons=0, pot=0.0):
        """Send this frame's state. The host sends STATE (ball + score + its
        paddle in `flags`); the guest sends INPUT (buttons + paddle knob).
        60 Hz-safe (~3% bus). No-op until start() has paired.
        Returns True if the frame left the station."""
        if self._role is None:
            return False
        try:
            import can, ustruct
            self._seq = (self._seq + 1) & 0xFF
            if self._role == "host":
                ustruct.pack_into("<HHBBBB", self._tx, 0,
                                  int(x) & 0xFFFF, int(y) & 0xFFFF,
                                  int(score_l) & 0xFF, int(score_r) & 0xFF,
                                  int(flags) & 0xFF, self._seq)
                return can.send(0x200, self._tx) is not False
            else:
                pot_u16 = int(max(0.0, min(1.0, pot)) * 4095)
                ustruct.pack_into("<BBHI", self._tx, 0,
                                  self._seq, int(buttons) & 0xFF, pot_u16, 0)
                return can.send(0x101, self._tx) is not False
        except Exception:
            return False

    def recv(self):
        """Return the NEWEST peer state as a dict, or None. Drains the ring
        (call every frame — game.link.stats' `dropped` counts what you miss).
        One-shot EVENTs never get lost to a later STATE: read them with
        game.link.event_poll()."""
        try:
            import can, ustruct, time
            newest = None
            frame = can.recv()
            while frame is not None:
                fid, data = frame
                if fid == 0x200 and len(data) == 8:
                    x, y, sl, sr, flags, seq = ustruct.unpack("<HHBBBB", data)
                    newest = {"kind": "state", "x": x, "y": y,
                              "score_l": sl, "score_r": sr,
                              "flags": flags, "seq": seq}
                elif (fid & 0x700) == 0x100 and len(data) == 8:
                    seq, btn, pot_u16, _ = ustruct.unpack("<BBHI", data)
                    newest = {"kind": "input", "seq": seq, "buttons": btn,
                              "pot": pot_u16 / 4095}
                elif fid == 0x300 and len(data) >= 2:
                    self._event = {"sfx": data[0], "event": data[1]}
                elif (fid & 0x7FE) == 0x7E0 and len(data) >= 6 and data[0] == 0x48:
                    # Peer restarted mid-match and is HELLOing: answer once so
                    # it can re-pair instead of "Linking..." forever.
                    n = ustruct.unpack_from("<I", data, 2)[0]
                    if n != self._nonce and self._hello is not None:
                        can.send(self._hello_id, self._hello)
                if (fid & 0x700) in (0x100, 0x200, 0x300) or (fid & 0x7F0) == 0x7E0 or (fid & 0x7FE) == 0x7F0:
                    self._last_rx = time.ticks_ms()
                frame = can.recv()
            return newest
        except Exception:
            return None

    def event_poll(self):
        """Return the latest peer EVENT ({"sfx", "event"}) once, else None."""
        e = self._event
        self._event = None
        return e

    def event(self, sfx_id=0, event_id=0):
        """Host: broadcast a game event (goal/win/serve) with its sound id."""
        try:
            import can
            return can.send(0x300, bytes((int(sfx_id) & 0xFF,
                                          int(event_id) & 0xFF))) is not False
        except Exception:
            return False

    def alive(self):
        """True while the peer is talking (any frame in the last 500 ms).
        Show a PAUSED overlay when this goes False. Also keeps our own 2 Hz
        heartbeat going so an idle console still looks alive to the peer."""
        try:
            import can, time
            now = time.ticks_ms()
            if time.ticks_diff(now, self._last_hb) >= 500:
                self._last_hb = now
                # id split by nonce LSB — same reason as HELLO (no equal-id TX)
                can.send(0x7F0 | ((self._nonce or 0) & 1), bytes((self._seq,)))
            if self._last_rx is None:
                return False
            return time.ticks_diff(now, self._last_rx) < 500
        except Exception:
            return False

    @property
    def role(self):
        return self._role


link = _Link()


# --- Scenery (ฉากหลังแบบเกม C แต่ราคาถูก) -----------------------------------
# ทุกตัวสร้างกล่องครั้งเดียวตอนจัดฉาก ไม่มีงานต่อเฟรม — แต่กินงบ widget ของจอ
# (จอมีได้สูงสุด 32 ชิ้นรวมทุกอย่าง) เลือกใช้พอประมาณนะ


def background(color=BLACK):
    """พื้นหลังเต็มจอ 1 ชิ้น — ต้องสร้างเป็น "อย่างแรก" ก่อนตัวละครทุกตัว
    (ของที่สร้างก่อนอยู่ล่างสุด สร้างทีหลังอยู่บนสุด)"""
    return Box(0, 0, WIDTH, HEIGHT, color)


def net(color=WHITE, dashes=14, dash_h=12):
    """เส้นเน็ตประกลางสนามแบบเกม Pong บนเครื่อง — ขีดกว้าง 4px สูง `dash_h`
    จำนวน `dashes` ขีดเรียงถี่แบบเน็ตของเกม C (ของจริงใช้ 17 ขีด แต่ 14 คือ
    ความหนาแน่นใกล้สุดที่ยังเหลืองบ widget ให้ตัวเกมครบทุกชิ้น)"""
    pitch = HEIGHT // dashes
    return [Box(WIDTH // 2 - 2, pitch // 2 - dash_h // 2 + i * pitch,
                4, dash_h, color)
            for i in range(dashes)]


def starfield(count=12, color=0x8899AA):
    """โปรยดาวให้ฉากอวกาศ (Shooter) — ดาวเล็ก 2px สีจาง และทุกดวงที่ 5 เป็น
    ดาวเด่น 3px สีขาว (สูตรเดียวกับฟ้าของเกม C) ตำแหน่งสุ่มแบบคำนวณซ้ำได้
    ฟ้าจะหน้าตาเหมือนเดิมทุกครั้งที่เปิดเกม"""
    seed = 0x2A5F
    stars = []
    for i in range(count):
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
        x = seed % (WIDTH - 4)
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
        y = seed % (HEIGHT - 48)
        if i % 5 == 4:                     # ดาวเด่น 1 ใน 5 ดวง แบบเกม C
            stars.append(Box(x, y, 3, 3, WHITE))
        else:
            stars.append(Box(x, y, 2, 2, color))
    return stars


def grass(height=24, color=GB_DARK):
    """แถบพื้นด้านล่างของจอ (เช่น พื้นดินของ Flappy)"""
    return Box(0, HEIGHT - height, WIDTH, height, color)


def jingle(notes=(60, 64, 67, 72), ms=90, wave=None, velocity=100):
    """ทำนองเปิดเกมสั้นๆ จากโน้ต MIDI (0 = เว้นจังหวะ) — ใส่สีสันให้หน้า title
    ลองแต่งของทีมเองดู: game.jingle((64, 0, 64, 67, 72), ms=110)"""
    if not _HAS_SOUND:
        return
    for n in notes:
        if n:
            tone(n, wave, velocity, ms)
        time.sleep_ms(ms + 20)


def _restart():
    """(ของเก่า) รีบูต MicroPython ทั้งตัว — ตอนนี้เกมใช้ game.replay() ที่ไวกว่าแทน"""
    try:
        import machine
        machine.soft_reset()
    except Exception:
        pass


def title(name, hint="Start = Play      Back = Quit", melody=None):
    """Show a title screen (the game name + how to control it), then wait.

    Use it in place of start() at the top of your game:
        game.title("CATCH")          # waits here
        # ...build your game...
        game.run(update, fps=30)

    START begins the game (this returns), BACK quits the whole program.
    Thai works on the board (current Game firmware) — keep Thai titles short.
    melody: ใส่ True = เล่นทำนองเปิดมาตรฐาน หรือส่ง tuple โน้ต MIDI ของทีมเอง
    """
    start()                                          # clear screen + wake joystick
    name = str(name)
    # The display font is ~8 px wide per character; center = middle - half the
    # text width (~4 px/char). (ui.Label has no align/width API to query.)
    Text(name, center_x(name), 150, CYAN)
    Text(hint, center_x(hint), 210, WHITE)
    if melody:                                       # ทำนองเปิดเกม (ถ้าอยากมี)
        jingle() if melody is True else jingle(melody)
    while True:
        if _script_done:                             # โหมดทดสอบอัตโนมัติ: ไม่รอปุ่มจริง
            clear()
            return
        k = keys()
        if k.start:                                  # START = เริ่มเล่น
            sfx("start")
            # Wait for the button to be released, otherwise run() would see this
            # same Start press on its first frame and immediately pause.
            while keys().start and not _script_done:
                time.sleep_ms(10)
            clear()
            return
        if k.back:                                   # BACK = ออกจากโปรแกรม
            while keys().back and not _script_done:
                time.sleep_ms(10)
            clear()
            raise SystemExit
        time.sleep_ms(30)


def run(update, fps=30, _script=None):
    """Run your game. `update` is a function called every frame.

    System buttons are handled for you on every Bento game:
      BACK  = ออกจากเกม กลับหน้า Bento Playground
      START = พักเกม (Pause) — กดอีกครั้งเพื่อเล่นต่อ ระหว่างพักเกมหยุดนิ่ง
    update() may also return False to stop on its own (e.g. GAME OVER).

    คืนค่า True เมื่อ update() หยุดเกมเอง (ตาย/จบเกม) และ False เมื่อผู้เล่นกด
    Back ออก — เอาไปต่อกับ game.game_over() เพื่อวนเล่นใหม่โดยไม่ต้องรีบูต:

        while True:
            build()                       # สร้างฉาก+ตัวละครของเกม
            if not game.run(update):      # Back = เลิกเล่น
                break
            if not game.game_over(score, name="snake"):
                break                     # Start = เล่นใหม่ / Back = ออก
    (_script ใช้กับระบบทดสอบอัตโนมัติของคอร์สเท่านั้น)
    """
    if _script is not None:
        _use_script(_script)
    delay = int(1000 / fps)
    # Persistent control hint, bottom-left, so the player always sees the keys.
    # ป้ายนี้ทำสองหน้าที่: ตอนพักเกมมันย้ายไปกลางจอเป็นป้าย PAUSED (ไม่กินงบเพิ่ม)
    hint_msg = "Start = Pause    Back = Quit"
    pause_msg = "PAUSED    Start = play"
    try:
        hint = Text(hint_msg, 12, HEIGHT - 24, GB_LIGHT)
    except Exception:
        hint = None                 # งบ widget เต็มพอดีก็แค่ไม่มีป้าย เกมยังเล่นได้
    paused = False
    prev_start = True     # กันปุ่ม Start ที่ยังกดค้างมาจากหน้าก่อนหน้า
    while True:
        # จับเวลาต้นเฟรมไว้ก่อน — ท้ายเฟรมจะได้หลับ "เท่าที่เหลือจริง" ของคาบ
        # (งานเฟรมไหนหนัก หลับสั้นลงเอง จังหวะเกมเลยตรงตาม fps แบบเกม C)
        t0 = time.ticks_ms()
        if _script_done:            # สคริปต์ทดสอบหมดเฟรม = จบการทดสอบ
            clear()
            return False
        pressed = keys()
        if pressed.back:            # BACK = ออกกลับ Playground
            clear()
            return False
        if pressed.start and not prev_start:   # START = พัก/เล่นต่อ (จับจังหวะกด)
            paused = not paused
            if hint is not None:
                if paused:          # ยืมป้ายคุมมาเป็นป้าย PAUSED กลางจอ
                    hint.set(pause_msg)
                    hint._label.pos(center_x(pause_msg),
                                    HEIGHT // 2 - 10)
                    try:
                        hint._label.color(CYAN)
                    except Exception:
                        pass
                else:               # เล่นต่อ: คืนป้ายกลับที่เดิม
                    hint.set(hint_msg)
                    hint._label.pos(12, HEIGHT - 24)
                    try:
                        hint._label.color(GB_LIGHT)
                    except Exception:
                        pass
            sfx("select")
        prev_start = pressed.start
        if paused:                  # ระหว่างพัก: ไม่เรียก update() ปุ่มทิศทางไม่มีผล
            time.sleep_ms(delay)
            continue
        if update() is False:
            return True
        # หลับเฉพาะเวลาที่เหลือของเฟรม (ticks_diff ปลอดภัยตอนตัวเลขนาฬิกาวน)
        time.sleep_ms(max(0, delay - time.ticks_diff(time.ticks_ms(), t0)))


# --- Game over / เล่นใหม่ / ตัวเลือกความยาก (จังหวะเดียวกับเกม C บนเครื่อง) ----
# เกม C เก็บ Best ไว้ตลอดที่เครื่องยังเปิดอยู่ (ปิดเครื่องแล้วเริ่มนับใหม่)
# ฝั่ง Python เราเก็บแบบเดียวกันเป๊ะ — dict ในหน่วยความจำ แยกตามชื่อเกม
_session_best = {}


def best(name="game"):
    """คะแนนดีที่สุดตั้งแต่เปิดเครื่อง (ช่อง Best แบบเดียวกับเกม C). ยังไม่เคยเล่น = 0"""
    return _session_best.get(name, 0)


def save_best(score, name="game"):
    """บันทึกคะแนนเข้าช่อง Best ของเกม (game.game_over() ทำให้อัตโนมัติอยู่แล้ว)
    เรียกเองเฉพาะทางลัดที่ไม่ผ่าน game_over — เช่น กด Y เริ่มตาใหม่ทันที หรือ
    กด Back ออกกลางคัน — คะแนนดีๆ ที่เพิ่งทำจะได้ไม่หายไปกับตานั้น"""
    if score is not None and score > _session_best.get(name, 0):
        _session_best[name] = score
    return _session_best.get(name, 0)


def replay():
    """ล้างจอ + ล้างสถานะปุ่ม เพื่อเริ่มเกมใหม่ทันที ไม่ต้องรีบูต MicroPython
    ปกติไม่ต้องเรียกเอง — game.game_over() จัดการให้ตอนผู้เล่นกด Start เล่นใหม่
    หลัง replay() วิดเจ็ตเก่าหายหมด ต้องสร้างฉากใหม่ทั้งชุด (ตามโครง while ของเกม)"""
    _edge_prev.clear()
    try:
        _r(ui.clear)
    except Exception:
        pass


def game_over(score=None, name="game", msg="GAME OVER"):
    """จบเกมแบบเดียวกับเกม C: ขึ้นป้ายคาไว้บนฉากที่ค้างอยู่ พร้อมคะแนนกับ Best
    แล้วรอผู้เล่นเลือก — Start = เล่นใหม่ (ล้างจอให้แล้ว คืน True)
                       Back  = เลิกเล่น (คืน False)

        if not game.game_over(score, name="snake"):
            break        # อยู่ใน while ใหญ่ของเกม (ดูตัวอย่างที่ game.run)

    หมายเหตุงบ widget: ป้ายชุดนี้ใช้เพิ่มชั่วคราว 4 ชิ้น — ถ้าเกมใช้ครบ 32 พอดี
    ป้ายบางส่วนจะไม่ขึ้น แต่ปุ่มยังทำงานปกติ (ตั้งใจให้พังแบบไม่ล้ม)"""
    save_best(score, name)
    # ป้ายซ้อนบนฉากเดิม สไตล์เดียวกับกล่องเมนูของเกม C (น้ำเงินเข้ม ขอบฟ้า มุมมน)
    try:
        Box(WIDTH // 2 - 180, HEIGHT // 2 - 70, 360, 140, 0x101A38,
            border=0x80F4FF, border_w=2, radius=10)
    except Exception:
        pass
    for text, dy, color in (
            (str(msg), -46, CYAN),
            ("" if score is None else
             "Score %d      Best %d" % (score, _session_best.get(name, 0)), -10, WHITE),
            ("Start = play again      Back = quit", 26, GB_LIGHT)):
        if not text:
            continue
        try:
            Text(text, center_x(text), HEIGHT // 2 + dy, color)
        except Exception:
            pass
    # รอปล่อยปุ่มก่อน (กันปุ่มที่กดค้างตอนตายกลายเป็นคำสั่งทันที)
    k = keys()
    while (k.start or k.back) and not _script_done:
        time.sleep_ms(10)
        k = keys()
    while True:
        if _script_done:            # โหมดทดสอบอัตโนมัติ: จบเกมเมื่อสคริปต์หมด
            clear()
            return False
        k = keys()
        if k.start:                 # START = เล่นใหม่
            sfx("start")
            while keys().start and not _script_done:
                time.sleep_ms(10)
            replay()
            return True
        if k.back:                  # BACK = พอแค่นี้
            while keys().back and not _script_done:
                time.sleep_ms(10)
            clear()
            return False
        time.sleep_ms(30)


def menu(*options, heading="SELECT MODE", selected=None):
    """หน้าเลือกโหมด/ความยาก หน้าตาและจังหวะเดียวกับเกม C บนเครื่อง:
    เลื่อนด้วย UP/DOWN (วนหัวท้าย), ยืนยันด้วย A หรือ Start. คืน index ที่เลือก
    กด BACK = ยกเลิก คืน None (เกมควรถือว่า "เลิกเล่น" — เหมือนเกม C ที่กด
    Back จากหน้าเลือกโหมดแล้วกลับ Playground):

        mode = game.menu("EASY", "NORMAL", "HARD")   # -> 0/1/2 หรือ None
        if mode is None:
            ...ออกจากเกม...

    เรียกก่อนสร้างฉากเกม (เมนูเก็บกวาดป้ายของตัวเองตอนจบ ของอย่างอื่นไม่โดนแตะ)
    ค่าเริ่มต้นชี้ตัวกลางเหมือนเกม C (NORMAL คือมาตรฐาน)"""
    if not options:
        return 0
    n = len(options)
    sel = selected if selected is not None else (1 if n >= 3 else 0)
    panel_h = 70 + 34 * n
    top = HEIGHT // 2 - panel_h // 2
    made = []          # เก็บทุกชิ้นที่เมนูสร้าง ไว้ลบทิ้งตอนออก

    def _make(builder):
        try:
            w = builder()
            made.append(w)
            return w
        except Exception:
            return None

    # กล่องเมนูขอบฟ้ามุมมน + หัวข้อสีขาว — ก็อปสเปกจากเมนูของเกม C ตรงๆ
    _make(lambda: Box(WIDTH // 2 - 160, top, 320, panel_h, 0x101A38,
                      border=0x80F4FF, border_w=2, radius=10))
    _make(lambda: Text(heading, center_x(str(heading)), top + 14, WHITE))
    rows = [_make(lambda i=i: Text("", WIDTH // 2 - (len(str(options[i])) + 6) * 4,
                                   top + 50 + i * 34, WHITE))
            for i in range(n)]

    def _paint():
        # ตัวที่เลือกเป็น ">  ชื่อ  <" สีฟ้า ตัวอื่นสีเทา — ลอกหน้าตามาจากเกม C
        for i in range(n):
            if rows[i] is None:
                continue
            if i == sel:
                rows[i].set(">  %s  <" % options[i])
            else:
                rows[i].set("   %s" % options[i])
            try:
                rows[i]._label.color(0x80F4FF if i == sel else 0x6C7CA8)
            except Exception:
                pass

    _paint()
    prev = keys()      # จำสถานะแรกไว้ กันปุ่มที่ค้างมาจากหน้า title
    while True:
        if _script_done:
            break
        k = keys()
        if k.back and not prev.back:       # BACK = ยกเลิก ออกจากหน้าเลือก
            sfx("back")
            sel = None
            break
        if k.up and not prev.up:
            sel = (sel + n - 1) % n
            sfx("move")
            _paint()
        elif k.down and not prev.down:
            sel = (sel + 1) % n
            sfx("move")
            _paint()
        if (k.a and not prev.a) or (k.start and not prev.start):
            sfx("select")
            break
        prev = k
        time.sleep_ms(30)
    # รอปล่อยปุ่มที่เพิ่งกด แล้วเก็บกวาดเฉพาะป้ายของเมนู
    k = keys()
    while (k.a or k.start or k.back) and not _script_done:
        time.sleep_ms(10)
        k = keys()
    for w in made:
        try:
            w.delete()
        except Exception:
            pass
    return sel
