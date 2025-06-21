#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <esp_task_wdt.h>

// Display pins
#define TFT_CS    5
#define TFT_RST   4
#define TFT_DC    2
// Hardware SPI uses default pins: MOSI=23, SCK=18

// Game pins
#define JUMP_PIN  13  // Using GPIO13 for jump button
#define BUZZER_PIN 21 // Piezo buzzer on GPIO21

// Display setup (Hardware SPI)
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// Game constants
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 280
#define GROUND_HEIGHT 50
#define DINO_WIDTH    20
#define DINO_HEIGHT   24
#define OBSTACLE_WIDTH 16
#define OBSTACLE_HEIGHT 32
#define GRAVITY       2
#define JUMP_STRENGTH 18
#define GAME_SPEED    4

// Colors - Synthwave palette
#define COLOR_BG      0x0000  // Black
#define COLOR_GROUND  0x4208  // Dark gray
#define COLOR_DINO    0x07E0  // Green
#define COLOR_OBSTACLE 0xF800 // Red
#define COLOR_TEXT    0xFFFF  // White
#define COLOR_CLOUD   0x8410  // Light gray
#define COLOR_NEON_PINK   0xF81F  // Neon pink/magenta
#define COLOR_NEON_CYAN   0x07FF  // Neon cyan
#define COLOR_NEON_PURPLE 0x781F  // Neon purple
#define COLOR_DARK_BLUE   0x0010  // Dark blue
#define COLOR_GRID_GREEN  0x0400  // Dark green for grid

// Game variables
struct Dino {
  int x, y;
  int prevX, prevY;  // Previous position for clearing
  int velY;
  bool isJumping;
  bool isDucking;
  bool needsRedraw;  // Flag to indicate if dino needs redrawing
} dino;

struct Obstacle {
  int x, y;
  int prevX;  // Previous X position for clearing
  bool active;
  bool needsRedraw;  // Flag to indicate if obstacle needs redrawing
} obstacles[2];

// Cloud positions for animation
struct Cloud {
  int x, y;
  int prevX;
  bool needsRedraw;
} clouds[3];

int gameSpeed = GAME_SPEED;
int score = 0;
int prevScore = -1;  // Previous score for selective update
int highScore = 0;
int prevHighScore = -1;  // Previous high score for selective update
bool gameOver = false;
bool gameStarted = false;
bool backgroundDrawn = false;  // Flag to track if background is drawn
unsigned long lastObstacle = 0;
unsigned long scoreTimer = 0;

// Sound functions
void playJumpSound() {
  // Quick ascending beep for jump
  tone(BUZZER_PIN, 800, 100);
}

void playScoreSound() {
  // Short high beep for score milestone
  tone(BUZZER_PIN, 1200, 50);
}

void playGameOverSound() {
  // Descending game over sound
  tone(BUZZER_PIN, 600, 200);
  delay(100);
  tone(BUZZER_PIN, 400, 200);
  delay(100);
  tone(BUZZER_PIN, 200, 400);
}

void playStartSound() {
  // Ascending start sound
  tone(BUZZER_PIN, 400, 100);
  delay(50);
  tone(BUZZER_PIN, 600, 100);
  delay(50);
  tone(BUZZER_PIN, 800, 150);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Disable watchdog timer for setup
  esp_task_wdt_delete(NULL);
  
  Serial.println("Initializing display...");
  
  // Initialize display
  tft.init(240, 280);
  tft.setRotation(3);  // Rotate 270 degrees (180 degrees from original)
  tft.fillScreen(COLOR_BG);
  
  Serial.println("Display initialized");
  
  // Initialize jump button
  pinMode(JUMP_PIN, INPUT_PULLUP);
  
  // Initialize buzzer
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  
  // Initialize game
  initGame();
  
  // Show start screen
  showStartScreen();
  
  Serial.println("Game ready!");
}

void loop() {
  yield();
  
  if (!gameStarted) {
    // Wait for jump button press to start
    if (digitalRead(JUMP_PIN) == LOW) {
      playStartSound();      // Play start sound
      gameStarted = true;
      initGame();
      drawInitialBackground();
      delay(200);
    }
    delay(50);
    return;
  }
  
  if (gameOver) {
    if (digitalRead(JUMP_PIN) == LOW) {
      gameOver = false;
      gameStarted = false;
      showStartScreen();
      delay(200);
    }
    delay(50);
    return;
  }
  
  // Store previous positions before updating
  storePreviousPositions();
  
  // Game logic
  handleInput();
  updateDino();
  updateObstacles();
  updateClouds();
  checkCollisions();
  updateScore();
  
  // Only redraw what has changed
  drawGameSelective();
  
  yield();
  delay(50);
}

void initGame() {
  // Initialize dino
  dino.x = 30;
  dino.y = SCREEN_HEIGHT - GROUND_HEIGHT - DINO_HEIGHT;
  dino.prevX = dino.x;
  dino.prevY = dino.y;
  dino.velY = 0;
  dino.isJumping = false;
  dino.isDucking = false;
  dino.needsRedraw = true;
  
  // Initialize obstacles
  for (int i = 0; i < 2; i++) {
    obstacles[i].active = false;
    obstacles[i].x = SCREEN_WIDTH + i * 200;
    obstacles[i].prevX = obstacles[i].x;
    obstacles[i].y = SCREEN_HEIGHT - GROUND_HEIGHT - OBSTACLE_HEIGHT;
    obstacles[i].needsRedraw = false;
  }
  
  // Initialize clouds
  for (int i = 0; i < 3; i++) {
    clouds[i].x = i * 80;
    clouds[i].prevX = clouds[i].x;
    clouds[i].y = 60 + i * 30;
    clouds[i].needsRedraw = true;
  }
  
  score = 0;
  prevScore = -1;
  gameSpeed = GAME_SPEED;
  lastObstacle = millis();
  scoreTimer = millis();
  backgroundDrawn = false;
}

void storePreviousPositions() {
  // Store dino's previous position
  dino.prevX = dino.x;
  dino.prevY = dino.y;
  
  // Store obstacles' previous positions
  for (int i = 0; i < 2; i++) {
    obstacles[i].prevX = obstacles[i].x;
  }
  
  // Store clouds' previous positions
  for (int i = 0; i < 3; i++) {
    clouds[i].prevX = clouds[i].x;
  }
}

void drawInitialBackground() {
  // Draw static background elements once
  tft.fillScreen(COLOR_BG);
  
  // Draw ground
  tft.fillRect(0, SCREEN_HEIGHT - GROUND_HEIGHT, SCREEN_WIDTH, GROUND_HEIGHT, COLOR_GROUND);
  tft.drawLine(0, SCREEN_HEIGHT - GROUND_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - GROUND_HEIGHT, COLOR_TEXT);
  
  backgroundDrawn = true;
  
  // Mark everything for initial draw
  dino.needsRedraw = true;
  for (int i = 0; i < 2; i++) {
    obstacles[i].needsRedraw = true;
  }
  for (int i = 0; i < 3; i++) {
    clouds[i].needsRedraw = true;
  }
}

void handleInput() {
  if (digitalRead(JUMP_PIN) == LOW && !dino.isJumping) {
    dino.isJumping = true;
    dino.velY = -JUMP_STRENGTH;
    playJumpSound();  // Play jump sound
  }
}

void updateDino() {
  if (dino.isJumping) {
    dino.y += dino.velY;
    dino.velY += GRAVITY;
    
    if (dino.y >= SCREEN_HEIGHT - GROUND_HEIGHT - DINO_HEIGHT) {
      dino.y = SCREEN_HEIGHT - GROUND_HEIGHT - DINO_HEIGHT;
      dino.isJumping = false;
      dino.velY = 0;
    }
  }
  
  // Check if dino position changed
  if (dino.x != dino.prevX || dino.y != dino.prevY) {
    dino.needsRedraw = true;
  }
}

void updateObstacles() {
  for (int i = 0; i < 2; i++) {
    if (obstacles[i].active) {
      obstacles[i].x -= gameSpeed;
      
      if (obstacles[i].x < -OBSTACLE_WIDTH) {
        obstacles[i].active = false;
        obstacles[i].needsRedraw = true;  // Need to clear it
      } else if (obstacles[i].x != obstacles[i].prevX) {
        obstacles[i].needsRedraw = true;
      }
    }
  }
  
  // Spawn new obstacles
  if (millis() - lastObstacle > random(2000, 4000)) {
    for (int i = 0; i < 2; i++) {
      if (!obstacles[i].active) {
        obstacles[i].active = true;
        obstacles[i].x = SCREEN_WIDTH;
        obstacles[i].y = SCREEN_HEIGHT - GROUND_HEIGHT - OBSTACLE_HEIGHT;
        obstacles[i].needsRedraw = true;
        lastObstacle = millis();
        break;
      }
    }
  }
}

void updateClouds() {
  for (int i = 0; i < 3; i++) {
    clouds[i].x = (i * 80 + (score / 2)) % (SCREEN_WIDTH + 40) - 20;
    
    if (clouds[i].x != clouds[i].prevX) {
      clouds[i].needsRedraw = true;
    }
  }
}

void checkCollisions() {
  for (int i = 0; i < 2; i++) {
    if (obstacles[i].active) {
      if (dino.x < obstacles[i].x + OBSTACLE_WIDTH &&
          dino.x + DINO_WIDTH > obstacles[i].x &&
          dino.y < obstacles[i].y + OBSTACLE_HEIGHT &&
          dino.y + DINO_HEIGHT > obstacles[i].y) {
        gameOver = true;
        if (score > highScore) {
          highScore = score;
        }
        playGameOverSound();  // Play game over sound
        showGameOver();
        return;
      }
    }
  }
}

void updateScore() {
  if (millis() - scoreTimer > 100) {
    score++;
    scoreTimer = millis();
    
    // Play sound every 100 points
    if (score % 100 == 0) {
      playScoreSound();
      if (gameSpeed < 8) {
        gameSpeed++;
      }
    }
  }
}

void clearRect(int x, int y, int w, int h) {
  // Clear a rectangle by filling it with background color
  // But preserve ground if we're clearing over it
  if (y + h > SCREEN_HEIGHT - GROUND_HEIGHT) {
    // Part of rectangle is over ground
    int groundOverlap = (y + h) - (SCREEN_HEIGHT - GROUND_HEIGHT);
    int bgHeight = h - groundOverlap;
    
    if (bgHeight > 0) {
      tft.fillRect(x, y, w, bgHeight, COLOR_BG);
    }
    if (groundOverlap > 0) {
      tft.fillRect(x, SCREEN_HEIGHT - GROUND_HEIGHT, w, groundOverlap, COLOR_GROUND);
    }
  } else {
    tft.fillRect(x, y, w, h, COLOR_BG);
  }
}

void drawGameSelective() {
  yield();
  
  // Clear and redraw dino if needed
  if (dino.needsRedraw) {
    // Clear previous position
    clearRect(dino.prevX, dino.prevY, DINO_WIDTH + 8, DINO_HEIGHT + 4);
    
    // Draw dino at new position
    drawDino();
    dino.needsRedraw = false;
  }
  
  // Clear and redraw obstacles if needed
  for (int i = 0; i < 2; i++) {
    if (obstacles[i].needsRedraw) {
      // Clear previous position
      clearRect(obstacles[i].prevX - 4, obstacles[i].y, OBSTACLE_WIDTH + 8, OBSTACLE_HEIGHT);
      
      // Draw obstacle at new position if still active
      if (obstacles[i].active) {
        drawObstacle(obstacles[i].x, obstacles[i].y);
      }
      obstacles[i].needsRedraw = false;
    }
  }
  
  yield();
  
  // Clear and redraw clouds if needed
  for (int i = 0; i < 3; i++) {
    if (clouds[i].needsRedraw) {
      // Clear previous position
      clearRect(clouds[i].prevX, clouds[i].y - 3, 24, 12);
      
      // Draw cloud at new position
      drawCloud(clouds[i].x, clouds[i].y);
      clouds[i].needsRedraw = false;
    }
  }
  
  // Update score display if changed
  if (score != prevScore) {
    // Clear score area
    tft.fillRect(10, 10, 150, 25, COLOR_BG);
    
    // Draw new score
    tft.setTextColor(COLOR_TEXT);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.print("Score: ");
    tft.print(score);
    
    prevScore = score;
  }
  
  // Update high score display if changed
  if (highScore != prevHighScore) {
    // Clear high score area
    tft.fillRect(10, 35, 150, 25, COLOR_BG);
    
    // Draw new high score
    tft.setTextColor(COLOR_TEXT);
    tft.setTextSize(2);
    tft.setCursor(10, 35);
    tft.print("High: ");
    tft.print(highScore);
    
    prevHighScore = highScore;
  }
}

void drawDino() {
  // Simple dino shape using rectangles
  tft.fillRect(dino.x, dino.y, DINO_WIDTH, DINO_HEIGHT, COLOR_DINO);
  
  // Dino head
  tft.fillRect(dino.x + 12, dino.y, 8, 8, COLOR_DINO);
  
  // Dino eye
  tft.fillRect(dino.x + 16, dino.y + 2, 2, 2, COLOR_BG);
  
  // Dino legs (simple animation based on score)
  if ((score / 5) % 2 == 0) {
    tft.fillRect(dino.x + 2, dino.y + DINO_HEIGHT, 4, 4, COLOR_DINO);
    tft.fillRect(dino.x + 12, dino.y + DINO_HEIGHT, 4, 4, COLOR_DINO);
  } else {
    tft.fillRect(dino.x + 6, dino.y + DINO_HEIGHT, 4, 4, COLOR_DINO);
    tft.fillRect(dino.x + 16, dino.y + DINO_HEIGHT, 4, 4, COLOR_DINO);
  }
}

void drawObstacle(int x, int y) {
  // Draw cactus-like obstacle
  tft.fillRect(x, y, OBSTACLE_WIDTH, OBSTACLE_HEIGHT, COLOR_OBSTACLE);
  
  // Add some detail to make it look like a cactus
  tft.fillRect(x - 4, y + 8, 8, 8, COLOR_OBSTACLE);
  tft.fillRect(x + OBSTACLE_WIDTH - 4, y + 12, 8, 8, COLOR_OBSTACLE);
}

void drawCloud(int x, int y) {
  // Only draw if cloud is visible on screen
  if (x > -24 && x < SCREEN_WIDTH) {
    tft.fillRect(x, y, 12, 6, COLOR_CLOUD);
    tft.fillRect(x + 6, y - 3, 12, 6, COLOR_CLOUD);
    tft.fillRect(x + 12, y, 12, 6, COLOR_CLOUD);
  }
}

// Function to draw custom "DINO SPIEL" text using geometric shapes
void drawCustomTitle() {
  int startX = 20;
  int startY = 20;
  int letterWidth = 25;
  int letterHeight = 30;
  int spacing = 5;
  
  // Draw "DINO" in neon pink
  uint16_t color1 = COLOR_NEON_PINK;
  
  // D
  int x = startX;
  tft.fillRect(x, startY, 4, letterHeight, color1);
  tft.fillRect(x, startY, letterWidth-8, 4, color1);
  tft.fillRect(x, startY + letterHeight-4, letterWidth-8, 4, color1);
  tft.fillRect(x + letterWidth-8, startY + 4, 4, letterHeight-8, color1);
  
  // I
  x += letterWidth + spacing;
  tft.fillRect(x, startY, letterWidth, 4, color1);
  tft.fillRect(x + (letterWidth/2-2), startY, 4, letterHeight, color1);
  tft.fillRect(x, startY + letterHeight-4, letterWidth, 4, color1);
  
  // N
  x += letterWidth + spacing;
  tft.fillRect(x, startY, 4, letterHeight, color1);
  tft.fillRect(x + letterWidth-4, startY, 4, letterHeight, color1);
  for (int i = 0; i < letterHeight; i += 2) {
    tft.fillRect(x + 4 + (i/2), startY + i, 2, 2, color1);
  }
  
  // O
  x += letterWidth + spacing;
  tft.fillRect(x, startY, letterWidth, 4, color1);
  tft.fillRect(x, startY + letterHeight-4, letterWidth, 4, color1);
  tft.fillRect(x, startY, 4, letterHeight, color1);
  tft.fillRect(x + letterWidth-4, startY, 4, letterHeight, color1);
  
  // Second line: "SPIEL" in neon cyan
  startY = 60;
  startX = 40;
  uint16_t color2 = COLOR_NEON_CYAN;
  
  // S
  x = startX;
  tft.fillRect(x, startY, letterWidth, 4, color2);
  tft.fillRect(x, startY, 4, letterHeight/2, color2);
  tft.fillRect(x, startY + letterHeight/2-2, letterWidth, 4, color2);
  tft.fillRect(x + letterWidth-4, startY + letterHeight/2, 4, letterHeight/2, color2);
  tft.fillRect(x, startY + letterHeight-4, letterWidth, 4, color2);
  
  // P
  x += letterWidth + spacing;
  tft.fillRect(x, startY, 4, letterHeight, color2);
  tft.fillRect(x, startY, letterWidth-4, 4, color2);
  tft.fillRect(x, startY + letterHeight/2-2, letterWidth-4, 4, color2);
  tft.fillRect(x + letterWidth-4, startY + 4, 4, letterHeight/2-6, color2);
  
  // I
  x += letterWidth + spacing;
  tft.fillRect(x, startY, letterWidth, 4, color2);
  tft.fillRect(x + (letterWidth/2-2), startY, 4, letterHeight, color2);
  tft.fillRect(x, startY + letterHeight-4, letterWidth, 4, color2);
  
  // E
  x += letterWidth + spacing;
  tft.fillRect(x, startY, 4, letterHeight, color2);
  tft.fillRect(x, startY, letterWidth, 4, color2);
  tft.fillRect(x, startY + letterHeight/2-2, letterWidth-8, 4, color2);
  tft.fillRect(x, startY + letterHeight-4, letterWidth, 4, color2);
  
  // L
  x += letterWidth + spacing;
  tft.fillRect(x, startY, 4, letterHeight, color2);
  tft.fillRect(x, startY + letterHeight-4, letterWidth, 4, color2);
}

void showStartScreen() {
  // Clear screen with black background
  tft.fillScreen(COLOR_BG);
  
  // Create gradient background from dark blue to black
  for (int y = 0; y < SCREEN_HEIGHT; y++) {
    uint8_t intensity = map(y, 0, SCREEN_HEIGHT, 20, 0);
    uint16_t gradColor = tft.color565(intensity/4, intensity/6, intensity);
    tft.drawLine(0, y, SCREEN_WIDTH-1, y, gradColor);
  }
  
  // Draw perspective grid (like in synthwave aesthetic)
  int gridSpacing = 20;
  int horizonY = SCREEN_HEIGHT/2 + 20;
  
  // Horizontal grid lines (perspective)
  for (int i = 0; i < 8; i++) {
    int y = horizonY + i * (gridSpacing + i * 2);
    if (y < SCREEN_HEIGHT) {
      tft.drawLine(0, y, SCREEN_WIDTH-1, y, COLOR_GRID_GREEN);
    }
  }
  
  // Vertical grid lines (perspective)
  int centerX = SCREEN_WIDTH / 2;
  for (int i = 1; i <= 6; i++) {
    // Left side lines
    int leftX = centerX - i * (gridSpacing - i);
    if (leftX > 0) {
      tft.drawLine(leftX, horizonY, leftX - i * 5, SCREEN_HEIGHT-1, COLOR_GRID_GREEN);
    }
    
    // Right side lines
    int rightX = centerX + i * (gridSpacing - i);
    if (rightX < SCREEN_WIDTH) {
      tft.drawLine(rightX, horizonY, rightX + i * 5, SCREEN_HEIGHT-1, COLOR_GRID_GREEN);
    }
  }
  
  // Draw neon mountains/triangles in background
  // Left mountain
  tft.drawTriangle(0, horizonY, 60, horizonY-40, 120, horizonY, COLOR_NEON_PURPLE);
  tft.drawTriangle(40, horizonY, 100, horizonY-60, 160, horizonY, COLOR_NEON_PINK);
  
  // Right mountain
  tft.drawTriangle(120, horizonY, 180, horizonY-50, 240, horizonY, COLOR_NEON_PURPLE);
  
  // Draw geometric sun/circle
  tft.drawCircle(SCREEN_WIDTH/2, horizonY-30, 25, COLOR_NEON_PINK);
  tft.drawCircle(SCREEN_WIDTH/2, horizonY-30, 23, COLOR_NEON_PINK);
  
  // Sun rays
  for (int i = 0; i < 8; i++) {
    float angle = i * 45 * PI / 180;
    int x1 = SCREEN_WIDTH/2 + cos(angle) * 30;
    int y1 = (horizonY-30) + sin(angle) * 30;
    int x2 = SCREEN_WIDTH/2 + cos(angle) * 40;
    int y2 = (horizonY-30) + sin(angle) * 40;
    tft.drawLine(x1, y1, x2, y2, COLOR_NEON_CYAN);
  }
  
  // Draw custom title "DINO SPIEL"
  drawCustomTitle();
  
  // Draw stylized dino silhouette in center
  int dinoX = SCREEN_WIDTH/2 - 15;
  int dinoY = 100;
  
  // Dino body with neon outline
  tft.drawRect(dinoX, dinoY, 20, 24, COLOR_NEON_CYAN);
  tft.drawRect(dinoX+1, dinoY+1, 18, 22, COLOR_NEON_CYAN);
  
  // Dino head
  tft.drawRect(dinoX + 12, dinoY, 8, 8, COLOR_NEON_CYAN);
  
  // Dino eye (glowing effect)
  tft.fillRect(dinoX + 16, dinoY + 2, 2, 2, COLOR_NEON_PINK);
  tft.drawPixel(dinoX + 15, dinoY + 2, COLOR_NEON_PINK);
  tft.drawPixel(dinoX + 18, dinoY + 2, COLOR_NEON_PINK);
  
  // Instructions with retro style
  tft.setTextSize(1);
  tft.setTextColor(COLOR_NEON_CYAN);
  tft.setCursor(70, SCREEN_HEIGHT - 50);
  tft.print("PRESS JUMP");
  
  tft.setTextColor(COLOR_NEON_PINK);
  tft.setCursor(80, SCREEN_HEIGHT - 40);
  tft.print("TO START");
  
  // High score display
  if (highScore > 0) {
    tft.setTextColor(COLOR_NEON_PURPLE);
    tft.setTextSize(1);
    tft.setCursor(10, SCREEN_HEIGHT - 30);
    tft.print("HIGH SCORE: ");
    tft.setTextColor(COLOR_NEON_PINK);
    tft.print(highScore);
  }
  
  // Add some decorative elements
  // Neon stars
  tft.fillRect(30, 30, 2, 2, COLOR_NEON_PINK);
  tft.fillRect(200, 40, 2, 2, COLOR_NEON_CYAN);
  tft.fillRect(50, 80, 2, 2, COLOR_NEON_PURPLE);
  tft.fillRect(180, 90, 2, 2, COLOR_NEON_PINK);
  
  // Larger decorative diamonds
  tft.drawPixel(100, 35, COLOR_NEON_CYAN);
  tft.drawPixel(99, 36, COLOR_NEON_CYAN);
  tft.drawPixel(101, 36, COLOR_NEON_CYAN);
  tft.drawPixel(100, 37, COLOR_NEON_CYAN);
  
  backgroundDrawn = false;  // Need to redraw background when game starts
}

void showGameOver() {
  // Draw game over overlay with neon style
  tft.fillRect(20, 90, 200, 100, COLOR_BG);
  tft.drawRect(20, 90, 200, 100, COLOR_NEON_PINK);
  tft.drawRect(21, 91, 198, 98, COLOR_NEON_PINK);
  
  tft.setTextColor(COLOR_NEON_CYAN);
  tft.setTextSize(3);
  tft.setCursor(40, 110);
  tft.print("GAME");
  tft.setCursor(45, 140);
  tft.print("OVER");
  
  tft.setTextSize(1);
  tft.setTextColor(COLOR_NEON_PINK);
  tft.setCursor(60, 170);
  tft.print("Press JUMP to restart");
}