#include "load_save_png.hpp"
#include "GL.hpp"
#include "Meshes.hpp"
#include "Scene.hpp"
#include "read_chunk.hpp"

#include <SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <fstream>

static GLuint compile_shader(GLenum type, std::string const &source);
static GLuint link_program(GLuint vertex_shader, GLuint fragment_shader);

int main(int argc, char **argv) {
    //Configuration:
    struct {
        std::string title = "Game2: Scene";
        glm::uvec2 size = glm::uvec2(640, 480);
    } config;

    //------------  initialization ------------

    //Initialize SDL library:
    SDL_Init(SDL_INIT_VIDEO);

    //Ask for an OpenGL context version 3.3, core profile, enable debug:
    SDL_GL_ResetAttributes();
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

    //create window:
    SDL_Window *window = SDL_CreateWindow(
        config.title.c_str(),
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        config.size.x, config.size.y,
        SDL_WINDOW_OPENGL /*| SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI*/
    );

    if (!window) {
        std::cerr << "Error creating SDL window: " << SDL_GetError() << std::endl;
        return 1;
    }

    //Create OpenGL context:
    SDL_GLContext context = SDL_GL_CreateContext(window);

    if (!context) {
        SDL_DestroyWindow(window);
        std::cerr << "Error creating OpenGL context: " << SDL_GetError() << std::endl;
        return 1;
    }

    #ifdef _WIN32
    //On windows, load OpenGL extensions:
    if (!init_gl_shims()) {
        std::cerr << "ERROR: failed to initialize shims." << std::endl;
        return 1;
    }
    #endif

    //Set VSYNC + Late Swap (prevents crazy FPS):
    if (SDL_GL_SetSwapInterval(-1) != 0) {
        std::cerr << "NOTE: couldn't set vsync + late swap tearing (" << SDL_GetError() << ")." << std::endl;
        if (SDL_GL_SetSwapInterval(1) != 0) {
            std::cerr << "NOTE: couldn't set vsync (" << SDL_GetError() << ")." << std::endl;
        }
    }

    //Hide mouse cursor (note: showing can be useful for debugging):
    //SDL_ShowCursor(SDL_DISABLE);

    //------------ opengl objects / game assets ------------

    //shader program:
    GLuint program = 0;
    GLuint program_Position = 0;
    GLuint program_Normal = 0;
    GLuint program_Colour = 0;
    GLuint program_mvp = 0;
    GLuint program_itmv = 0;
    GLuint program_to_light = 0;
    { //compile shader program:
        GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER,
            "#version 330\n"
            "uniform mat4 mvp;\n"
            "uniform mat3 itmv;\n"
            "in vec4 Position;\n"
            "in vec3 Normal;\n"
            "out vec3 normal;\n"
            "void main() {\n"
            "	gl_Position = mvp * Position;\n"
            "	normal = itmv * Normal;\n"
            "}\n"
        );

        //n.l = light intensity ~ RGB
        GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER,
            "#version 330\n"
            "uniform vec3 to_light;\n"
            "in vec3 normal;\n"
            "in vec3 Colour;"
            "out vec4 fragColor;\n"
            "void main() {\n"
            "	float light = max(0.0, dot(normalize(normal), to_light));\n"
            "	fragColor = vec4(light * Colour, 1.0);\n"
            "}\n"
        );

        program = link_program(fragment_shader, vertex_shader);

        //look up attribute locations:
        program_Position = glGetAttribLocation(program, "Position");
        if (program_Position == -1U) throw std::runtime_error("no attribute named Position");
        program_Normal = glGetAttribLocation(program, "Normal");
        if (program_Normal == -1U) throw std::runtime_error("no attribute named Normal");
        program_Colour = glGetAttribLocation(program, "Colour");
        if (program_Colour == -1U) throw std::runtime_error("no attribute named Colour");

        //look up uniform locations:
        program_mvp = glGetUniformLocation(program, "mvp");
        if (program_mvp == -1U) throw std::runtime_error("no uniform named mvp");
        program_itmv = glGetUniformLocation(program, "itmv");
        if (program_itmv == -1U) throw std::runtime_error("no uniform named itmv");

        program_to_light = glGetUniformLocation(program, "to_light");
        if (program_to_light == -1U) throw std::runtime_error("no uniform named to_light");
    }

    //------------ meshes ------------

    Meshes meshes;

    { //add meshes to database:
        Meshes::Attributes attributes;
        attributes.Position = program_Position;
        attributes.Normal = program_Normal;
        attributes.Colour = program_Colour;

        meshes.load("meshes.blob", attributes);
    }
    
    //------------ scene ------------

    Scene scene;
    //set up camera parameters based on window:
    scene.camera.fovy = glm::radians(60.0f);
    scene.camera.aspect = float(config.size.x) / float(config.size.y);
    scene.camera.near = 0.01f;
    //(transform will be handled in the update function below)

    //add some objects from the mesh library:
    auto add_object = [&](std::string const &name, glm::vec3 const &position, glm::quat const &rotation, glm::vec3 const &scale) -> Scene::Object & {
        Mesh const &mesh = meshes.get(name);
        scene.objects.emplace_back();
        Scene::Object &object = scene.objects.back();
        object.transform.position = position;
        object.transform.rotation = rotation;
        object.transform.scale = scale;
        object.vao = mesh.vao;
        object.start = mesh.start;
        object.count = mesh.count;
        object.program = program;
        object.program_mvp = program_mvp;
        object.program_itmv = program_itmv;
        return object;
    };


    { //read objects to add from "scene.blob":
        std::ifstream file("scene.blob", std::ios::binary);

        std::vector< char > strings;
        //read strings chunk:
        read_chunk(file, "str0", &strings);

        { //read scene chunk, add meshes to scene:
            struct SceneEntry {
                uint32_t name_begin, name_end;
                glm::vec3 position;
                glm::quat rotation;
                glm::vec3 scale;
            };
            static_assert(sizeof(SceneEntry) == 48, "Scene entry should be packed");

            std::vector< SceneEntry > data;
            read_chunk(file, "scn0", &data);

            for (auto const &entry : data) {
                if (!(entry.name_begin <= entry.name_end && entry.name_end <= strings.size())) {
                    throw std::runtime_error("index entry has out-of-range name begin/end");
                }
                std::string name(&strings[0] + entry.name_begin, &strings[0] + entry.name_end);
                //do not add robot moving parts and balloons (not sure how period is processed)
                if (name.find("Cube") >= 0 || name.find("Crate") >= 0 || name.compare("Stand") == 0) {
                    add_object(name, entry.position, entry.rotation, entry.scale);
                }
            }
        }
    }

    //add balloons using blender coords (test)
    Scene::Object * balloon_1 = &add_object("Balloon1.001", glm::vec3(-0.03f, 2.61f, 1.18f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f));
    Scene::Object * balloon_2 = &add_object("Balloon2.001", glm::vec3(-1.43f, 0.81f, 2.19f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f));
    Scene::Object * balloon_3 = &add_object("Balloon3.001", glm::vec3(0.85f, -2.08f, 2.42f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f));

    //balloon pop animation
    Scene::Object * balloon_pop_1;
    Scene::Object * balloon_pop_2;
    Scene::Object * balloon_pop_3;

    //set bounce movement parameters
    int bounce_dir = 1;
    float bounce_speed = 0.2f;

    //set game state
    std::vector< bool > popped(3, false);
    bool popped_all = popped[0] && popped[1] && popped[2];
    //starts when balloon is popped
    std::vector< float > pop_timer(3, 0.0f);

    //create a robot stack:
    std::vector< Scene::Object * > robot_stack;
    //coordinates from blender (test)
    robot_stack.emplace_back( &add_object("Base", glm::vec3(0.0f, 0.0f, 0.0f), glm::quat(0.0f, 0.0f, 0.0f, 1.0f), glm::vec3(1.0f)) );
    robot_stack.emplace_back( &add_object("Link1", glm::vec3(0.0f, 0.0f, 0.6f), glm::quat(0.0f, 0.0f, 0.0f, 1.0f), glm::vec3(1.0f)) );
    robot_stack.emplace_back( &add_object("Link2", glm::vec3(0.0f, 0.0f, 1.8f), glm::quat(0.0f, 0.0f, 0.0f, 1.0f), glm::vec3(1.0f)) );
    robot_stack.emplace_back( &add_object("Link3", glm::vec3(0.0f, 0.0f, 3.0f), glm::quat(0.0f, 0.0f, 0.0f, 1.0f), glm::vec3(1.0f)) );

    for (uint32_t i = 1; i < robot_stack.size(); ++i) {
        robot_stack[i]->transform.set_parent(&robot_stack[i-1]->transform);
    }

    //vector of rotations
    std::vector< float > joint_rot(robot_stack.size(), 0.0f);
    //vector of rotation axes
    std::vector< glm::vec3 > rot_axes(robot_stack_size(), glm::vec3(1.0f, 0.0f, 0.0f));
    rot_axes[0] = vec3(0.0f, 0.0f, 1.0f); // only base rotates about z axis
    //angular velocity
    float ang_velocity = 0.1f;

    glm::vec2 mouse = glm::vec2(0.0f, 0.0f); //mouse position in [-1,1]x[-1,1] coordinates

    struct {
        float radius = 5.0f;
        float elevation = 0.0f;
        float azimuth = 0.0f;
        glm::vec3 target = glm::vec3(0.0f, 0.0f, 0.0f);
    } camera;



    //------------ game loop ------------

    bool should_quit = false;
    while (true) {
        static SDL_Event evt;
        while (SDL_PollEvent(&evt)) {
            //handle input:
            if (evt.type == SDL_MOUSEMOTION) {
                glm::vec2 old_mouse = mouse;
                mouse.x = (evt.motion.x + 0.5f) / float(config.size.x) * 2.0f - 1.0f;
                mouse.y = (evt.motion.y + 0.5f) / float(config.size.y) *-2.0f + 1.0f;
                if (evt.motion.state & SDL_BUTTON(SDL_BUTTON_LEFT)) {
                    camera.elevation += -2.0f * (mouse.y - old_mouse.y);
                    camera.azimuth += -2.0f * (mouse.x - old_mouse.x);
                }
            } else if (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_ESCAPE) {
                should_quit = true;
            } else if (evt.type == SDL_QUIT) {
                should_quit = true;
                break;
            }

        }
        if (should_quit) break;

        auto current_time = std::chrono::high_resolution_clock::now();
        static auto previous_time = current_time;
        float elapsed = std::chrono::duration< float >(current_time - previous_time).count();
        previous_time = current_time;

        { //update game state:
            
            //get key presses; TODO: possible refactoring
            static const uint8 * keys = SDL_GetKeyboardState(Null);
            if (keys[SDL_SCANCODE_Z]) {
                joint_rot[0] += ang_velocity * elapsed;
            } else if (keys[SDL_SCANCODE_X]) {
                joint_rot[0] -= ang_velocity * elapsed;
            } else if (keys[SDL_SCANCODE_A]) {
                joint_rot[1] += ang_velocity * elapsed;
            } else if (keys[SDL_SCANCODE_S]) {
                joint_rot[1] -= ang_velocity * elapsed;
            } else if (keys[SDL_SCANCODE_SEMICOLON]) {
                joint_rot[2] = ang_velocity * elapsed;
            } else if (keys[SDL_SCANCODE_COMMA]) {
                joint_rot[2] = ang_velocity * elapsed;
            } else if (keys[SDL_SCANCODE_PERIOD]) {
                joint_rot[3] = ang_velocity * elapsed;
            } else if (keys[SDL_SCANCODE_SLASH]) {
                joint_rot[3] = ang_velocity * elapsed;
            }

            //robot stack:
            for (uint32_t i = 0; i < robot_stack.size(); ++i) {
                float ang = fmod((2 + joint_rot[i] * 0.5f) * float(M_PI), 2 * float(M_PI));
                robot_stack[i]->transform.rotation = glm::angleAxis(ang, rot_axes[i]);
            }

            //balloon bounce
            balloon_1->transform.position.z += elapsed * bounce_speed * bounce_dir;
            balloon_2->transform.position.z -= elapsed * bounce_speed * bounce_dir;
            balloon_3->transform.position.z += elapsed * bounce_speed * bounce_dir;
            bounce_dir *= -1;

            //balloon and pin distance
            //note we don't use sqrt since dis <= 1 <-> dis^2 <=1 if real
            glm::vec3 pin = robot_stack[robot_stack.size() - 1]->transform.position;
            if (dot(balloon_1->transform.position - pin, balloon_1->transform.position - pin) <= 1.0f) {
                balloon_pop_1 = &add_object("Balloon1-Pop", 
                                            balloon_1->transform.position, 
                                            balloon_1->transform.rotation, 
                                            balloon_1->transform.scale);
                balloon_1->transform.scale = glm::vec3(0.0f);
                balloon_1->transform.transformation = glm::(0.0f, 0.0f, 50.0f);
                popped[0] = true;
            }
            if (dot(balloon_2->transform.position - pin, balloon_1->transform.position - pin) <= 1.0f) {
                balloon_pop_2 = &add_object("Balloon2-Pop",
                    balloon_2->transform.position,
                    balloon_2->transform.rotation,
                    balloon_2->transform.scale);
                balloon_2->transform.scale = glm::vec3(0.0f);
                balloon_2->transform.transformation = glm::(0.0f, 0.0f, 50.0f);
                popped[1] = true;
            }
            if (dot(balloon_3->transform.position - pin, balloon_3->transform.position - pin) <= 1.0f) {
                balloon_pop_3 = &add_object("Balloon2-Pop",
                    balloon_3->transform.position,
                    balloon_3->transform.rotation,
                    balloon_3->transform.scale);
                balloon_3->transform.scale = glm::vec3(0.0f);
                balloon_3->transform.transformation = glm::(0.0f, 0.0f, 50.0f);
                popped[2] = true;
            }

            //check balloon pop
            for (uint32_t i = 0; i < popped.size(); ++i) {
                if (popped[i]) {
                    pop_timer[i] += elapsed;
                    if (pop_timer[i] >= 0.2f) {
                        switch (i) {
                            case 0:
                                balloon_pop_1->transform.scale = glm::vec3(0.0f);
                            case 1:
                                balloon_pop_2->transform.scale = glm::vec3(0.0f);
                            case 2:
                                balloon_pop_3->transform.scale = glm::vec3(0.0f);
                            default: /* should not reach */ break;
                        }
                    }
                }
            }

            popped_all = popped[0] && popped[1] && popped[2];
            if (pop_all) {
                should_quit = true;
            }

            //camera:
            scene.camera.transform.position = camera.radius * glm::vec3(
                std::cos(camera.elevation) * std::cos(camera.azimuth),
                std::cos(camera.elevation) * std::sin(camera.azimuth),
                std::sin(camera.elevation)) + camera.target;

            glm::vec3 out = -glm::normalize(camera.target - scene.camera.transform.position);
            glm::vec3 up = glm::vec3(0.0f, 0.0f, 1.0f);
            up = glm::normalize(up - glm::dot(up, out) * out);
            glm::vec3 right = glm::cross(up, out);
            
            scene.camera.transform.rotation = glm::quat_cast(
                glm::mat3(right, up, out)
            );
            scene.camera.transform.scale = glm::vec3(1.0f, 1.0f, 1.0f);
        }

        //draw output:
        glClearColor(0.5, 0.5, 0.5, 0.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


        { //draw game state:
            glUseProgram(program);
            glUniform3fv(program_to_light, 1, glm::value_ptr(glm::normalize(glm::vec3(0.0f, 1.0f, 10.0f))));
            scene.render();
        }


        SDL_GL_SwapWindow(window);
    }


    //------------  teardown ------------

    SDL_GL_DeleteContext(context);
    context = 0;

    SDL_DestroyWindow(window);
    window = NULL;

    return 0;
}



static GLuint compile_shader(GLenum type, std::string const &source) {
    GLuint shader = glCreateShader(type);
    GLchar const *str = source.c_str();
    GLint length = source.size();
    glShaderSource(shader, 1, &str, &length);
    glCompileShader(shader);
    GLint compile_status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);
    if (compile_status != GL_TRUE) {
        std::cerr << "Failed to compile shader." << std::endl;
        GLint info_log_length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_log_length);
        std::vector< GLchar > info_log(info_log_length, 0);
        GLsizei length = 0;
        glGetShaderInfoLog(shader, info_log.size(), &length, &info_log[0]);
        std::cerr << "Info log: " << std::string(info_log.begin(), info_log.begin() + length);
        glDeleteShader(shader);
        throw std::runtime_error("Failed to compile shader.");
    }
    return shader;
}

static GLuint link_program(GLuint fragment_shader, GLuint vertex_shader) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    GLint link_status = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &link_status);
    if (link_status != GL_TRUE) {
        std::cerr << "Failed to link shader program." << std::endl;
        GLint info_log_length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info_log_length);
        std::vector< GLchar > info_log(info_log_length, 0);
        GLsizei length = 0;
        glGetProgramInfoLog(program, info_log.size(), &length, &info_log[0]);
        std::cerr << "Info log: " << std::string(info_log.begin(), info_log.begin() + length);
        throw std::runtime_error("Failed to link program");
    }
    return program;
}
