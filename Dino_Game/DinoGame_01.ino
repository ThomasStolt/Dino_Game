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
#define JUMP_STRENGTH 15
#define GAME_SPEED    4

// Game variables
struct Dino {
  int x, y;
  int velY;
  bool isJumping;
  bool isDucking;
} dino;

struct Obstacle {
  int x, y;
  bool active;
} obstacles[3];

int gameSpeed = GAME_SPEED;
int score = 0;
int highScore = 0;
bool gameOver = false;
bool gameStarted = false;
unsigned long lastObstacle = 0;
unsigned long scoreTimer = 0;

// Colors
#define COLOR_BG      0x0000  // Black
#define COLOR_GROUND  0x4208  // Dark gray
#define COLOR_DINO    0x07E0  // Green
#define COLOR_OBSTACLE 0xF800 // Red
#define COLOR_TEXT    0xFFFF  // White
#define COLOR_CLOUD   0x8410  // Light gray

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Disable watchdog timer for setup
  esp_task_wdt_delete(NULL);
  
  Serial.println("Initializing display...");
  
  // Initialize display
  tft.init(240, 280);
  tft.setRotation(3);
  tft.fillScreen(COLOR_BG);
  
  Serial.println("Display initialized");
  
  // Initialize jump button
  pinMode(JUMP_PIN, INPUT_PULLUP);
  
  // Initialize game
  initGame();
  
  // Show start screen
  showStartScreen();
  
  Serial.println("Game ready!");
}

void loop() {
  // Feed the watchdog
  yield();
  
  if (!gameStarted) {
    if (digitalRead(JUMP_PIN) == LOW) {
      gameStarted = true;
      initGame();
      delay(200); // Debounce
    }
    delay(50);
    return;
  }
  
  if (gameOver) {
    if (digitalRead(JUMP_PIN) == LOW) {
      gameOver = false;
      gameStarted = false;
      showStartScreen();
      delay(200); // Debounce
    }
    delay(50);
    return;
  }
  
  // Game logic
  handleInput();
  updateDino();
  updateObstacles();
  checkCollisions();
  updateScore();
  
  // Draw everything
  drawGame();
  
  // Yield control to prevent watchdog reset
  yield();
  delay(50); // Game loop delay
}

void initGame() {
  // Initialize dino
  dino.x = 30;
  dino.y = SCREEN_HEIGHT - GROUND_HEIGHT - DINO_HEIGHT;
  dino.velY = 0;
  dino.isJumping = false;
  dino.isDucking = false;
  
  // Initialize obstacles
  for (int i = 0; i < 3; i++) {
    obstacles[i].active = false;
    obstacles[i].x = SCREEN_WIDTH + i * 150;
    obstacles[i].y = SCREEN_HEIGHT - GROUND_HEIGHT - OBSTACLE_HEIGHT;
  }
  
  score = 0;
  gameSpeed = GAME_SPEED;
  lastObstacle = millis();
  scoreTimer = millis();
}

void handleInput() {
  if (digitalRead(JUMP_PIN) == LOW && !dino.isJumping) {
    dino.isJumping = true;
    dino.velY = -JUMP_STRENGTH;
  }
}

void updateDino() {
  if (dino.isJumping) {
    dino.y += dino.velY;
    dino.velY += GRAVITY;
    
    // Check if dino landed
    if (dino.y >= SCREEN_HEIGHT - GROUND_HEIGHT - DINO_HEIGHT) {
      dino.y = SCREEN_HEIGHT - GROUND_HEIGHT - DINO_HEIGHT;
      dino.isJumping = false;
      dino.velY = 0;
    }
  }
}

void updateObstacles() {
  // Move existing obstacles
  for (int i = 0; i < 3; i++) {
    if (obstacles[i].active) {
      obstacles[i].x -= gameSpeed;
      
      // Remove obstacle if it's off screen
      if (obstacles[i].x < -OBSTACLE_WIDTH) {
        obstacles[i].active = false;
      }
    }
  }
  
  // Spawn new obstacles
  if (millis() - lastObstacle > random(1500, 3000)) {
    for (int i = 0; i < 3; i++) {
      if (!obstacles[i].active) {
        obstacles[i].active = true;
        obstacles[i].x = SCREEN_WIDTH;
        obstacles[i].y = SCREEN_HEIGHT - GROUND_HEIGHT - OBSTACLE_HEIGHT;
        lastObstacle = millis();
        break;
      }
    }
  }
}

void checkCollisions() {
  for (int i = 0; i < 3; i++) {
    if (obstacles[i].active) {
      // Simple rectangle collision
      if (dino.x < obstacles[i].x + OBSTACLE_WIDTH &&
          dino.x + DINO_WIDTH > obstacles[i].x &&
          dino.y < obstacles[i].y + OBSTACLE_HEIGHT &&
          dino.y + DINO_HEIGHT > obstacles[i].y) {
        gameOver = true;
        if (score > highScore) {
          highScore = score;
        }
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
    
    // Increase speed every 100 points
    if (score % 100 == 0 && gameSpeed < 8) {
      gameSpeed++;
    }
  }
}

void drawGame() {
  // Yield before heavy drawing operations
  yield();
  
  // Clear screen
  tft.fillScreen(COLOR_BG);
  
  // Draw ground
  tft.fillRect(0, SCREEN_HEIGHT - GROUND_HEIGHT, SCREEN_WIDTH, GROUND_HEIGHT, COLOR_GROUND);
  
  // Draw ground line
  tft.drawLine(0, SCREEN_HEIGHT - GROUND_HEIGHT, SCREEN_WIDTH, SCREEN_HEIGHT - GROUND_HEIGHT, COLOR_TEXT);
  
  // Yield between drawing operations
  yield();
  
  // Draw dino
  drawDino();
  
  // Draw obstacles
  for (int i = 0; i < 3; i++) {
    if (obstacles[i].active) {
      drawObstacle(obstacles[i].x, obstacles[i].y);
    }
  }
  
  yield();
  
  // Draw clouds (simple decoration)
  drawClouds();
  
  // Draw score
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("Score: ");
  tft.print(score);
  
  tft.setCursor(10, 35);
  tft.print("High: ");
  tft.print(highScore);
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

void drawClouds() {
  // Simple cloud decoration
  int cloudY = 60;
  for (int i = 0; i < 3; i++) {
    int cloudX = (i * 80 + (score / 2)) % (SCREEN_WIDTH + 40) - 20;
    
    // Cloud shape using circles (rectangles for simplicity)
    tft.fillRect(cloudX, cloudY + i * 30, 12, 6, COLOR_CLOUD);
    tft.fillRect(cloudX + 6, cloudY + i * 30 - 3, 12, 6, COLOR_CLOUD);
    tft.fillRect(cloudX + 12, cloudY + i * 30, 12, 6, COLOR_CLOUD);
  }
}

void showStartScreen() {
  tft.fillScreen(COLOR_BG);
  
  // Title
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(3);
  tft.setCursor(60, 50);
  tft.print("DINO");
  tft.setCursor(50, 80);
  tft.print("GAME");
  
  // Instructions
  tft.setTextSize(2);
  tft.setCursor(30, 130);
  tft.print("Press JUMP");
  tft.setCursor(50, 155);
  tft.print("to start");
  
  // High score
  if (highScore > 0) {
    tft.setCursor(30, 200);
    tft.print("High Score:");
    tft.setCursor(70, 225);
    tft.print(highScore);
  }
  
  // Draw a simple dino
  tft.fillRect(100, 160, 20, 24, COLOR_DINO);
  tft.fillRect(112, 160, 8, 8, COLOR_DINO);
  tft.fillRect(116, 162, 2, 2, COLOR_BG);
}

void showGameOver() {
  // Draw game over overlay
  tft.fillRect(20, 90, 200, 100, COLOR_BG);
  tft.drawRect(20, 90, 200, 100, COLOR_TEXT);
  
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(3);
  tft.setCursor(40, 110);
  tft.print("GAME");
  tft.setCursor(45, 140);
  tft.print("OVER");
  
  tft.setTextSize(1);
  tft.setCursor(60, 170);
  tft.print("Press JUMP to restart");
}