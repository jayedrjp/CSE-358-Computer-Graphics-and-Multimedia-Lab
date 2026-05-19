/*
 * ============================================================
 *  SNAKE GAME — OpenGL 3.3 Core Profile
 * ============================================================
 *  Features:
 *   - Classic snake movement on a 20x20 grid
 *   - Food spawns randomly
 *   - Snake grows when eating food
 *   - Speed increases every 5 points
 *   - Walls and self-collision end the game
 *   - Score displayed in window title
 *   - Press R to restart after game over
 *
 *  Controls:
 *   Arrow Keys or WASD  — move snake
 *   R                   — restart after game over
 *   ESC                 — quit
 *
 *  Build:
 *   g++ snake_game.cpp glad.c -o snake_game -lglfw3 -lopengl32   (Windows)
 *   g++ snake_game.cpp glad.c -o snake_game -lglfw -lGL -ldl      (Linux)
 *
 *  Dependencies:
 *   glad.h / glad.c
 *   glfw3.h
 *   glm/
 * ============================================================
 */

#include "glad.h"
#include "glfw3.h"

#include "glm/glm/glm.hpp"
#include "glm/glm/gtc/matrix_transform.hpp"
#include "glm/glm/gtc/type_ptr.hpp"

#include <iostream>
#include <vector>
#include <deque>
#include <string>
#include <cstdlib>
#include <ctime>

// ─────────────────────────────────────────────
//  Function Declarations
// ─────────────────────────────────────────────
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);

// ─────────────────────────────────────────────
//  Window & Grid Settings
// ─────────────────────────────────────────────
const unsigned int SCR_WIDTH  = 600;
const unsigned int SCR_HEIGHT = 600;

const int GRID_COLS = 20;
const int GRID_ROWS = 20;

// ─────────────────────────────────────────────
//  Shader Sources  (class style: const char* with \n)
// ─────────────────────────────────────────────
const char* vertexShaderSource =
    "#version 330 core\n"
    "layout (location = 0) in vec3 aPos;\n"
    "uniform mat4 transform;\n"
    "void main()\n"
    "{\n"
    "   gl_Position = transform * vec4(aPos, 1.0);\n"
    "}\0";

const char* fragmentShaderSource =
    "#version 330 core\n"
    "out vec4 FragColor;\n"
    "uniform vec4 uColor;\n"
    "void main()\n"
    "{\n"
    "   FragColor = uColor;\n"
    "}\n\0";

// ─────────────────────────────────────────────
//  Direction Enum
// ─────────────────────────────────────────────
enum Direction { UP, DOWN, LEFT, RIGHT };

// ─────────────────────────────────────────────
//  Grid Cell
// ─────────────────────────────────────────────
struct Cell {
    int x, y;
};

// ─────────────────────────────────────────────
//  Game State  (global so key_callback can access it)
// ─────────────────────────────────────────────
struct GameState {
    std::deque<Cell> snake;   // front = head
    Cell             food;
    Direction        dir;
    Direction        nextDir;
    bool             alive;
    int              score;
    float            moveInterval;   // seconds per step
    float            timeSinceMove;
};

GameState gs;

// ─────────────────────────────────────────────
//  Spawn food at a random unoccupied cell
// ─────────────────────────────────────────────
void spawnFood()
{
    Cell f;
    bool occupied = true;
    while (occupied) {
        f.x = rand() % GRID_COLS;
        f.y = rand() % GRID_ROWS;
        occupied = false;
        for (auto& seg : gs.snake) {
            if (seg.x == f.x && seg.y == f.y) {
                occupied = true;
                break;
            }
        }
    }
    gs.food = f;
}

// ─────────────────────────────────────────────
//  Initialize / Reset game state
// ─────────────────────────────────────────────
void initGame()
{
    gs.snake.clear();

    int startX = GRID_COLS / 2;
    int startY = GRID_ROWS / 2;

    gs.snake.push_back({startX,     startY});   // head
    gs.snake.push_back({startX - 1, startY});   // body
    gs.snake.push_back({startX - 2, startY});   // tail

    gs.dir           = RIGHT;
    gs.nextDir       = RIGHT;
    gs.alive         = true;
    gs.score         = 0;
    gs.moveInterval  = 0.20f;
    gs.timeSinceMove = 0.0f;

    spawnFood();
}

// ─────────────────────────────────────────────
//  Build a glm transform matrix that places a
//  unit quad at grid cell (col, row)
//
//  Strategy (same as class files: glm transform uniform):
//   1. Scale the unit quad to one cell size in NDC
//   2. Translate it to the correct grid position
//
//  NDC: X in [-1, 1], Y in [-1, 1]
//  Each cell is (2.0/GRID_COLS) wide, (2.0/GRID_ROWS) tall
// ─────────────────────────────────────────────
glm::mat4 cellTransform(int col, int row, float paddingPx = 2.0f)
{
    // Cell size in NDC
    float cellW_ndc = 2.0f / GRID_COLS;
    float cellH_ndc = 2.0f / GRID_ROWS;

    // Padding shrinks the cell slightly to create grid-line gaps
    float padW = paddingPx / (SCR_WIDTH  / GRID_COLS);  // padding as fraction of cell
    float padH = paddingPx / (SCR_HEIGHT / GRID_ROWS);

    float scaleX = cellW_ndc * (1.0f - padW);
    float scaleY = cellH_ndc * (1.0f - padH);

    // Top-left of cell in NDC, then shift to cell center
    // Row 0 is TOP of screen, so Y is flipped
    float offsetX = -1.0f + col * cellW_ndc + cellW_ndc * 0.5f;
    float offsetY =  1.0f - row * cellH_ndc - cellH_ndc * 0.5f;

    glm::mat4 transform = glm::mat4(1.0f);
    transform = glm::translate(transform, glm::vec3(offsetX, offsetY, 0.0f));
    transform = glm::scale(transform, glm::vec3(scaleX, scaleY, 1.0f));
    return transform;
}

// ─────────────────────────────────────────────
//  Draw one cell at (col, row) with given color
// ─────────────────────────────────────────────
void drawCell(unsigned int shaderProgram, unsigned int VAO,
              int col, int row,
              float r, float g, float b, float a = 1.0f,
              float padding = 2.0f)
{
    glm::mat4 transform = cellTransform(col, row, padding);

    unsigned int transformLoc = glGetUniformLocation(shaderProgram, "transform");
    glUniformMatrix4fv(transformLoc, 1, GL_FALSE, glm::value_ptr(transform));

    unsigned int colorLoc = glGetUniformLocation(shaderProgram, "uColor");
    glUniform4f(colorLoc, r, g, b, a);

    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);   // 2 triangles = 1 quad
}

// ─────────────────────────────────────────────
//  MAIN
// ─────────────────────────────────────────────
int main()
{
    srand(static_cast<unsigned int>(time(nullptr)));

    // ── GLFW init ──
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Snake Game -- Score: 0", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetKeyCallback(window, key_callback);

    // ── GLAD init ──
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    // ── Compile vertex shader ──
    unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
    glCompileShader(vertexShader);

    int success;
    char infoLog[512];
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
    }

    // ── Compile fragment shader ──
    unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
    glCompileShader(fragmentShader);

    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl;
    }

    // ── Link shaders ──
    unsigned int shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, vertexShader);
    glAttachShader(shaderProgram, fragmentShader);
    glLinkProgram(shaderProgram);

    glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
    if (!success)
    {
        glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
        std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // ── Unit quad: 6 vertices for 2 triangles, centered at origin ──
    // Vertices in [-0.5, 0.5] so glm::scale controls the size cleanly
    float vertices[] = {
        // First triangle
         0.5f,  0.5f, 0.0f,   // top right
         0.5f, -0.5f, 0.0f,   // bottom right
        -0.5f, -0.5f, 0.0f,   // bottom left
        // Second triangle
         0.5f,  0.5f, 0.0f,   // top right
        -0.5f, -0.5f, 0.0f,   // bottom left
        -0.5f,  0.5f, 0.0f    // top left
    };

    unsigned int VBO, VAO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // ── Init game ──
    initGame();

    // ── Timing ──
    float lastFrame = (float)glfwGetTime();

    // ── Render loop ──
    while (!glfwWindowShouldClose(window))
    {
        // ── Delta time ──
        float currentFrame = (float)glfwGetTime();
        float deltaTime    = currentFrame - lastFrame;
        lastFrame          = currentFrame;

        // ── Update game logic ──
        if (gs.alive)
        {
            gs.timeSinceMove += deltaTime;

            if (gs.timeSinceMove >= gs.moveInterval)
            {
                gs.timeSinceMove = 0.0f;

                // Apply queued direction
                gs.dir = gs.nextDir;

                // Calculate new head position
                Cell newHead = gs.snake.front();
                if (gs.dir == UP)    newHead.y -= 1;
                if (gs.dir == DOWN)  newHead.y += 1;
                if (gs.dir == LEFT)  newHead.x -= 1;
                if (gs.dir == RIGHT) newHead.x += 1;

                // Wall collision check
                if (newHead.x < 0 || newHead.x >= GRID_COLS ||
                    newHead.y < 0 || newHead.y >= GRID_ROWS)
                {
                    gs.alive = false;
                    glfwSetWindowTitle(window,
                        ("GAME OVER! Score: " + std::to_string(gs.score) +
                         " | Press R to restart").c_str());
                }
                else
                {
                    // Self collision check
                    bool selfHit = false;
                    for (auto& seg : gs.snake) {
                        if (seg.x == newHead.x && seg.y == newHead.y) {
                            selfHit = true;
                            break;
                        }
                    }

                    if (selfHit)
                    {
                        gs.alive = false;
                        glfwSetWindowTitle(window,
                            ("GAME OVER! Score: " + std::to_string(gs.score) +
                             " | Press R to restart").c_str());
                    }
                    else
                    {
                        // Move snake: add new head
                        gs.snake.push_front(newHead);

                        // Food eaten?
                        if (newHead.x == gs.food.x && newHead.y == gs.food.y)
                        {
                            // Grow: don't remove tail
                            gs.score++;
                            spawnFood();

                            glfwSetWindowTitle(window,
                                ("Snake Game -- Score: " +
                                 std::to_string(gs.score)).c_str());

                            // Speed up every 5 points, minimum 0.07s
                            if (gs.score % 5 == 0 && gs.moveInterval > 0.07f)
                                gs.moveInterval -= 0.02f;
                        }
                        else
                        {
                            // No food: remove tail to keep length
                            gs.snake.pop_back();
                        }
                    }
                }
            }
        }

        // ── Render ──
        glUseProgram(shaderProgram);

        glClearColor(0.10f, 0.10f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Draw faint grid background
        for (int row = 0; row < GRID_ROWS; row++) {
            for (int col = 0; col < GRID_COLS; col++) {
                drawCell(shaderProgram, VAO, col, row,
                         0.15f, 0.15f, 0.15f, 1.0f, 1.0f);
            }
        }

        // Draw snake
        if (gs.alive)
        {
            bool isHead = true;
            for (auto& seg : gs.snake)
            {
                if (isHead) {
                    // Head: bright green
                    drawCell(shaderProgram, VAO, seg.x, seg.y,
                             0.40f, 0.95f, 0.40f, 1.0f, 2.0f);
                    isHead = false;
                } else {
                    // Body: darker green
                    drawCell(shaderProgram, VAO, seg.x, seg.y,
                             0.20f, 0.75f, 0.20f, 1.0f, 2.0f);
                }
            }
        }
        else
        {
            // Game over: flash snake red
            for (auto& seg : gs.snake) {
                drawCell(shaderProgram, VAO, seg.x, seg.y,
                         0.85f, 0.15f, 0.15f, 1.0f, 2.0f);
            }
        }

        // Draw food: bright red
        drawCell(shaderProgram, VAO,
                 gs.food.x, gs.food.y,
                 0.95f, 0.25f, 0.25f, 1.0f, 2.0f);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // ── Cleanup ──
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shaderProgram);

    glfwTerminate();
    return 0;
}

// ─────────────────────────────────────────────
//  Key callback
// ─────────────────────────────────────────────
void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
{
    if (action != GLFW_PRESS) return;

    if (key == GLFW_KEY_ESCAPE)
        glfwSetWindowShouldClose(window, true);

    // Restart on R if dead
    if (key == GLFW_KEY_R && !gs.alive) {
        initGame();
        glfwSetWindowTitle(window, "Snake Game - Score: 0");
        return;
    }

    if (!gs.alive) return;

    // Queue next direction — block 180-degree reversal
    if ((key == GLFW_KEY_UP    || key == GLFW_KEY_W) && gs.dir != DOWN)
        gs.nextDir = UP;
    if ((key == GLFW_KEY_DOWN  || key == GLFW_KEY_S) && gs.dir != UP)
        gs.nextDir = DOWN;
    if ((key == GLFW_KEY_LEFT  || key == GLFW_KEY_A) && gs.dir != RIGHT)
        gs.nextDir = LEFT;
    if ((key == GLFW_KEY_RIGHT || key == GLFW_KEY_D) && gs.dir != LEFT)
        gs.nextDir = RIGHT;
}

// ─────────────────────────────────────────────
//  Framebuffer resize callback
// ─────────────────────────────────────────────
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}

/*
 * ─────────────────────────────────────────────────────────────
 *  HOW THE CODE WORKS (for your lab report)
 * ─────────────────────────────────────────────────────────────
 *
 *  RENDERING (matches class style):
 *   One unit quad VAO (vertices ±0.5, centered at origin) is created ONCE.
 *   Every rectangle reuses the same VAO.
 *   A glm::mat4 transform (translate + scale) is computed per cell and
 *   passed as a uniform — exactly like the class translation/scaling labs.
 *
 *  DATA STRUCTURE:
 *   std::deque<Cell> snake — double-ended queue of grid cells.
 *   Move  = push_front(newHead) + pop_back()
 *   Grow  = push_front(newHead) only (no pop_back)
 *
 *  TIMING:
 *   Delta time accumulates in timeSinceMove.
 *   Snake steps when timeSinceMove >= moveInterval (frame-rate independent).
 *   Speed increases by reducing moveInterval every 5 points.
 *
 *  COLLISION:
 *   Wall: new head x/y outside [0, GRID_COLS/ROWS) → game over.
 *   Self: new head matches any body segment → game over.
 *
 *  INPUT:
 *   key_callback buffers direction in nextDir.
 *   Reverse direction (e.g. LEFT while moving RIGHT) is blocked.
 * ─────────────────────────────────────────────────────────────
 */ 