#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

// ============================================================
// PINS
// ============================================================

#define BUTTON_PIN 4

#define OLED_SDA 21
#define OLED_SCL 22

// ============================================================
// OLED
// ============================================================

U8G2_SH1106_128X64_NONAME_F_HW_I2C display(
    U8G2_R0,
    U8X8_PIN_NONE
);

// ============================================================
// DINO GAME
// ============================================================

const int GROUND_Y = 56;

int dinoX = 15;
int dinoY = GROUND_Y - 16;

int velocityY = 0;

const int GRAVITY = 1;
const int JUMP_FORCE = -9;

bool jumping = false;

// Simple obstacle
int cactusX = 128;

// Timing
unsigned long lastFrame = 0;
const int FRAME_TIME = 50;   // ~20 FPS

// ============================================================
// DRAW DINO
// ============================================================

void drawDino(int x, int y)
{
    // Body
    display.drawBox(x + 3, y + 5, 12, 10);

    // Head
    display.drawBox(x + 10, y, 9, 10);

    // Eye
    display.setDrawColor(0);
    display.drawPixel(x + 16, y + 3);
    display.setDrawColor(1);

    // Tail
    display.drawLine(x + 3, y + 7, x, y + 4);
    display.drawLine(x + 3, y + 9, x, y + 9);

    // Legs
    if (!jumping) {
        display.drawLine(x + 6, y + 15, x + 6, y + 18);
        display.drawLine(x + 13, y + 15, x + 13, y + 18);
    } else {
        display.drawLine(x + 6, y + 15, x + 3, y + 17);
        display.drawLine(x + 13, y + 15, x + 16, y + 17);
    }
}

// ============================================================
// DRAW CACTUS
// ============================================================

void drawCactus(int x)
{
    display.drawBox(x, GROUND_Y - 15, 4, 15);

    display.drawBox(x - 4, GROUND_Y - 11, 4, 4);
    display.drawBox(x - 4, GROUND_Y - 13, 2, 6);

    display.drawBox(x + 4, GROUND_Y - 8, 4, 4);
    display.drawBox(x + 6, GROUND_Y - 11, 2, 7);
}

// ============================================================
// DRAW GROUND
// ============================================================

void drawGround()
{
    display.drawHLine(0, GROUND_Y, 128);

    // Small moving ground marks
    for (int x = 0; x < 128; x += 16) {
        display.drawHLine(x, GROUND_Y + 4, 5);
    }
}

// ============================================================
// JUMP
// ============================================================

void jump()
{
    if (!jumping) {
        jumping = true;
        velocityY = JUMP_FORCE;
    }
}

// ============================================================
// UPDATE DINO
// ============================================================

void updateDino()
{
    if (jumping) {

        dinoY += velocityY;
        velocityY += GRAVITY;

        if (dinoY >= GROUND_Y - 16) {
            dinoY = GROUND_Y - 16;
            velocityY = 0;
            jumping = false;
        }
    }
}

// ============================================================
// UPDATE CACTUS
// ============================================================

void updateCactus()
{
    cactusX -= 4;

    if (cactusX < -10) {
        cactusX = 128;
    }
}

// ============================================================
// DRAW SCREEN
// ============================================================

void drawGame()
{
    display.clearBuffer();

    drawDino(dinoX, dinoY);
    drawCactus(cactusX);
    drawGround();

    display.sendBuffer();
}

// ============================================================
// BUTTON
// ============================================================

void checkButton()
{
    static bool lastButtonState = HIGH;

    bool currentState = digitalRead(BUTTON_PIN);

    // Detect button press
    if (lastButtonState == HIGH && currentState == LOW) {
        jump();
    }

    lastButtonState = currentState;
}

// ============================================================
// SETUP
// ============================================================

void setup()
{
    Serial.begin(115200);

    // Button
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    // OLED
    Wire.begin(OLED_SDA, OLED_SCL);

    display.begin();

    display.clearBuffer();

    display.setFont(u8g2_font_7x13B_tr);
    display.drawStr(25, 25, "DINO TEST");

    display.setFont(u8g2_font_6x10_tr);
    display.drawStr(20, 45, "PRESS BUTTON");

    display.sendBuffer();

    delay(1500);

    // Initial game state
    dinoY = GROUND_Y - 16;
    cactusX = 128;
}

// ============================================================
// LOOP
// ============================================================

void loop()
{
    checkButton();

    unsigned long now = millis();

    if (now - lastFrame >= FRAME_TIME) {

        lastFrame = now;

        updateDino();
        updateCactus();

        drawGame();
    }
}