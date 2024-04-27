#include "SDL2/SDL.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

typedef struct{
    SDL_Window *window;
    SDL_Renderer *renderer;
} sdl_t;

typedef struct{
    uint32_t window_width;  //SDL window width
    uint32_t window_height; //SDL window height
    uint32_t fg_color;      //Foreground color rgba8888
    uint32_t bg_color;      //background color rgba8888
    uint32_t scale_factor;  //scale factor for window
    bool draw_pixel_outline;//enable/disable pixel outline
} config_t;

typedef enum
{
    QUIT,
    RUNNING,
    PAUSED,
} emulator_state_t;

typedef struct{
    uint16_t opcode;
    uint16_t NNN;   //12 bit address/constant
    uint8_t NN;     //8 bit constant
    uint8_t N;      //4 bit constant
    uint8_t X;      //4 bit register identifier
    uint8_t Y;      //4 bit register identifier
} instr_t;

typedef struct{
    emulator_state_t state;
    uint8_t ram[4096];
    bool display[64*32];
    uint16_t pc;
    uint16_t stack[12];
    uint16_t *stack_ptr;
    uint8_t V[16];
    uint16_t I;
    uint8_t delay_timer;
    uint8_t sound_timer;
    bool keypad[16];
    char *rom_name;
    instr_t instr;
} chip8_t;

//Initialize SDL
bool init_sdl(sdl_t *sdl, const config_t config){
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0){
        SDL_Log("SDL Initialize failed! %s\n", SDL_GetError());
        return false;
    }

    sdl->window = SDL_CreateWindow(
        "Chip-8 Interpreter",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        config.window_width*config.scale_factor,
        config.window_height*config.scale_factor,
        0);

    if(!sdl->window){
        SDL_Log("Could not create SDL Window! %s", SDL_GetError());
        return false;
    }

    sdl->renderer = SDL_CreateRenderer(sdl->window, -1, SDL_RENDERER_ACCELERATED);

    if(!sdl->renderer){
        SDL_Log("Could not create SDL Renderer! %s", SDL_GetError());
        return false;
    }

    return true;
}

//Setup emulator config from command arguments
bool set_config_from_args(config_t *config, int argc, char** argv){
    //Set default values
    *config = (config_t){
        .window_width = 64,
        .window_height = 32,
        .fg_color = 0xFFFFFFFF,
        .bg_color = 0x00000000,
        .scale_factor = 20,
        .draw_pixel_outline = false,
    };

    //Override defaults from passed arguments
    for (int i = 1; i < argc; i++){
        (void)argv[i];//prevent compiler error from unused argc argv
        if(strcmp(argv[i], "-d") == 0){
            config->draw_pixel_outline = true;
            printf("[Config] Draw outline enabled!\n");
        }
    }
    return true;
}


bool init_chip8(chip8_t *chip8, char *rom_name){

    const uint32_t entry_point = 0x200;//ROMS are loaded at this addr
    const uint8_t font[] = {
        0xF0, 0x90, 0x90, 0x90, 0xF0,		// 0
	    0x20, 0x60, 0x20, 0x20, 0x70,		// 1
	    0xF0, 0x10, 0xF0, 0x80, 0xF0,		// 2
	    0xF0, 0x10, 0xF0, 0x10, 0xF0,		// 3
	    0x90, 0x90, 0xF0, 0x10, 0x10,		// 4
	    0xF0, 0x80, 0xF0, 0x10, 0xF0,		// 5
	    0xF0, 0x80, 0xF0, 0x90, 0xF0,		// 6
	    0xF0, 0x10, 0x20, 0x40, 0x40,		// 7
	    0xF0, 0x90, 0xF0, 0x90, 0xF0,		// 8
	    0xF0, 0x90, 0xF0, 0x10, 0xF0,		// 9
	    0xF0, 0x90, 0xF0, 0x90, 0x90,		// A
	    0xE0, 0x90, 0xE0, 0x90, 0xE0,		// B
	    0xF0, 0x80, 0x80, 0x80, 0xF0,		// C
	    0xE0, 0x90, 0x90, 0x90, 0xE0,		// D
	    0xF0, 0x80, 0xF0, 0x80, 0xF0,		// E
	    0xF0, 0x80, 0xF0, 0x80, 0x80,		// F
    };
    
    //Load Fonts
    memcpy(&chip8->ram[0], font, sizeof(font));

    //Load ROM
    FILE *rom = fopen(rom_name, "rb");
    if(!rom){
        printf("Rom file %s is invalid or doesn't exist\n", rom_name);
        exit(1);
    }
    //get and check rom size
    fseek(rom, 0, SEEK_END);
    const size_t rom_size = ftell(rom);
    const size_t max_size = sizeof(chip8->ram) - entry_point;
    rewind(rom);
    if(rom_size > max_size){
        printf("[~] Rom file \"%s\" exceeds size of memory. Rom size: %zu, Max size allowed: %zu\n", rom_name, rom_size, max_size);
        exit(1);
    }

    if(fread(&chip8->ram[entry_point], rom_size, 1, rom) != 1){
        printf("[~] Rom file \"%s\" could not be read into CHIP-8 memory\n", rom_name, rom_size, max_size);
        exit(1);
    }
    
    printf("[~] Rom file \"%s\" succesfully loaded into CHIP-8 memory!\n", rom_name);

    fclose(rom);

    //Set defaults
    chip8->state = RUNNING;
    chip8->pc = entry_point;
    chip8->rom_name = rom_name;
    chip8->stack_ptr = &chip8->stack[0];
    return true;
}

//Final cleanup
void finalcleanup(sdl_t sdl){
    SDL_DestroyRenderer(sdl.renderer);
    SDL_DestroyWindow(sdl.window);
    SDL_Quit();
}

//Clear screen/SDL Window to bg color
void clear_screen(sdl_t sdl, config_t config){
    const uint8_t r = (config.bg_color >> 24) & 0xFF;
    const uint8_t g = (config.bg_color >> 16) & 0xFF;
    const uint8_t b = (config.bg_color >> 8) & 0xFF;
    const uint8_t a = (config.bg_color >> 0) & 0xFF;
    SDL_SetRenderDrawColor(sdl.renderer, r, g, b, a);
    SDL_RenderClear(sdl.renderer);
}

//update window with any changes
void update_screen(sdl_t sdl, chip8_t chip8, config_t config){
    SDL_Rect rect = {.x = 0, .y = 0, .w = config.scale_factor, .h = config.scale_factor};

    uint8_t fg_r = (config.fg_color >> 24) & 0xFF;
    uint8_t fg_g = (config.fg_color >> 16) & 0xFF;
    uint8_t fg_b = (config.fg_color >> 8) & 0xFF;
    uint8_t fg_a = (config.fg_color >> 0) & 0xFF;

    uint8_t bg_r = (config.bg_color >> 24) & 0xFF;
    uint8_t bg_g = (config.bg_color >> 16) & 0xFF;
    uint8_t bg_b = (config.bg_color >> 8) & 0xFF;
    uint8_t bg_a = (config.bg_color >> 0) & 0xFF;

    for (uint32_t i = 0; i < sizeof(chip8.display); i++){
        rect.x = (i % config.window_width) * config.scale_factor;
        rect.y = (i / config.window_width) * config.scale_factor;

        if(chip8.display[i]){
            SDL_SetRenderDrawColor(sdl.renderer, fg_r, fg_g, fg_b, fg_a);
            SDL_RenderFillRect(sdl.renderer, &rect);
            if(config.draw_pixel_outline){
                SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
                SDL_RenderDrawRect(sdl.renderer, &rect);
            }
        }else{
            SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
            SDL_RenderFillRect(sdl.renderer, &rect);
        }
    }
    SDL_RenderPresent(sdl.renderer);
}

void handle_input(chip8_t *chip8){
    SDL_Event event;
    while(SDL_PollEvent(&event)){
        switch(event.type){
            case SDL_QUIT:
                chip8->state = QUIT;
                printf("[State] CHIP-8 quit!");
                return;
            case SDL_KEYDOWN:
                switch(event.key.keysym.sym){
                    case SDLK_ESCAPE:
                        chip8->state = QUIT;
                        printf("[State] CHIP-8 quit!");
                        return;
                    case SDLK_SPACE:
                        if(chip8->state == RUNNING){
                            chip8->state = PAUSED;
                            printf("[State] CHIP-8 paused! Press SPACE to resume.\n");
                        }else{
                            chip8->state = RUNNING;
                            printf("[State] CHIP-8 resumed! Press SPACE to pause.\n");
                        }
                        return;    
                    case SDLK_1:chip8->keypad[0x1] = true;break;
                    case SDLK_2:chip8->keypad[0x2] = true;break;
                    case SDLK_3:chip8->keypad[0x3] = true;break;
                    case SDLK_4:chip8->keypad[0xC] = true;break;

                    case SDLK_q:chip8->keypad[0x4] = true;break;
                    case SDLK_w:chip8->keypad[0x5] = true;break;
                    case SDLK_e:chip8->keypad[0x6] = true;break;
                    case SDLK_r:chip8->keypad[0xD] = true;break;

                    case SDLK_a:chip8->keypad[0x7] = true;break;
                    case SDLK_s:chip8->keypad[0x8] = true;break;
                    case SDLK_d:chip8->keypad[0x9] = true;break;
                    case SDLK_f:chip8->keypad[0xE] = true;break;

                    case SDLK_z:chip8->keypad[0xA] = true;break;
                    case SDLK_x:chip8->keypad[0x0] = true;break;
                    case SDLK_c:chip8->keypad[0xB] = true;break;
                    case SDLK_v:chip8->keypad[0xF] = true;break;
                    
                    default:
                        break;
                }
                break;
            case SDL_KEYUP:
                switch(event.key.keysym.sym){
                    
                    case SDLK_1:chip8->keypad[0x1] = false;break;
                    case SDLK_2:chip8->keypad[0x2] = false;break;
                    case SDLK_3:chip8->keypad[0x3] = false;break;
                    case SDLK_4:chip8->keypad[0xC] = false;break;

                    case SDLK_q:chip8->keypad[0x4] = false;break;
                    case SDLK_w:chip8->keypad[0x5] = false;break;
                    case SDLK_e:chip8->keypad[0x6] = false;break;
                    case SDLK_r:chip8->keypad[0xD] = false;break;

                    case SDLK_a:chip8->keypad[0x7] = false;break;
                    case SDLK_s:chip8->keypad[0x8] = false;break;
                    case SDLK_d:chip8->keypad[0x9] = false;break;
                    case SDLK_f:chip8->keypad[0xE] = false;break;

                    case SDLK_z:chip8->keypad[0xA] = false;break;
                    case SDLK_x:chip8->keypad[0x0] = false;break;
                    case SDLK_c:chip8->keypad[0xB] = false;break;
                    case SDLK_v:chip8->keypad[0xF] = false;break;
                    
                    default:
                        break;
                }
                break;
            default:
                break;
        }
    }
}

void emulate_instr(chip8_t *chip8, config_t config){
    chip8->instr.opcode = chip8->ram[chip8->pc] << 8 | chip8->ram[chip8->pc + 1];
    
    chip8->pc += 2;

    chip8->instr.NNN = chip8->instr.opcode & 0x0FFF;
    chip8->instr.NN = chip8->instr.opcode & 0x0FF;
    chip8->instr.N = chip8->instr.opcode & 0x0F;
    chip8->instr.X = (chip8->instr.opcode >> 8) & 0x0F;
    chip8->instr.Y = (chip8->instr.opcode >> 4) & 0x0F;

    switch(chip8->instr.opcode >> 12){
        case 0x00:
            if(chip8->instr.NN == 0xE0){
                memset(&chip8->display, 0, sizeof(chip8->display));
            }else if(chip8->instr.NN == 0xEE){
                //return from subroutine
                chip8->pc = *--chip8->stack_ptr;
            }
            break;

        case 0x01:
            chip8->pc = chip8->instr.NNN;
            break;

        case 0x02:
            *chip8->stack_ptr++ = chip8->pc;
            chip8->pc = chip8->instr.NNN;
            break;

        case 0x03:
            if(chip8->V[chip8->instr.X] == chip8->instr.NN){
                chip8->pc += 2;
            }
            break;
        
        case 0x04:
            if(chip8->V[chip8->instr.X] != chip8->instr.NN){
                chip8->pc += 2;
            }
            break;

        case 0x05:
            if(chip8->instr.N != 0){
                break;
            }

            if(chip8->V[chip8->instr.X] == chip8->V[chip8->instr.Y]){
                chip8->pc += 2;
            }
            break;

        case 0x06:
            chip8->V[chip8->instr.X] = chip8->instr.NN;
            break;

        case 0x07:
            chip8->V[chip8->instr.X] += chip8->instr.NN;
            break;

        case 0x08:
            switch(chip8->instr.N){
                case 0:
                    chip8->V[chip8->instr.X] = chip8->V[chip8->instr.Y];
                    break;
                case 1:
                    chip8->V[chip8->instr.X] |= chip8->V[chip8->instr.Y];
                    break;
                case 2:
                    chip8->V[chip8->instr.X] &= chip8->V[chip8->instr.Y];
                    break;
                case 3:
                    chip8->V[chip8->instr.X] ^= chip8->V[chip8->instr.Y];
                    break;
                case 4:
                    if(chip8->V[chip8->instr.X] + chip8->V[chip8->instr.Y] > 255){
                        chip8->V[0x0F] = 1;
                    }else{
                        chip8->V[0x0F] = 0;
                    }
                    chip8->V[chip8->instr.X] += chip8->V[chip8->instr.Y];
                    break;
                case 5:
                    if(chip8->V[chip8->instr.Y] > chip8->V[chip8->instr.X]){
                        chip8->V[0x0F] = 0;
                    }else{
                        chip8->V[0x0F] = 1;
                    }
                    chip8->V[chip8->instr.X] -= chip8->V[chip8->instr.Y];
                    break;
                case 6:
                    chip8->V[0x0F] = chip8->V[chip8->instr.X] & 0x01;
                    chip8->V[chip8->instr.X] >>= 1;
                    break;
                case 7:
                    if(chip8->V[chip8->instr.X] > chip8->V[chip8->instr.Y]){
                        chip8->V[0x0F] = 0;
                    }else{
                        chip8->V[0x0F] = 1;
                    }
                    chip8->V[chip8->instr.X] = chip8->V[chip8->instr.Y] - chip8->V[chip8->instr.X];
                    break;
                case 0xE:
                    chip8->V[0x0F] = (chip8->V[chip8->instr.X] & 0x80) >> 7;
                    chip8->V[chip8->instr.X] <<= 1;
                    break;
                default:
                    break;
            }

        case 0x09:
            if(chip8->instr.N != 0){
                break;
            }
            if(chip8->V[chip8->instr.X] != chip8->V[chip8->instr.Y]){
                chip8->pc += 2;
            }
            break;

        case 0x0A:
            chip8->I = chip8->instr.NNN;
            break;

        case 0x0B:
            chip8->pc = chip8->V[0] + chip8->instr.NNN;
            break;

        case 0x0C:
            chip8->V[chip8->instr.X] = chip8->instr.NN & (rand() % 256);
            break;

        case 0x0D:;
            uint8_t x = chip8->V[chip8->instr.X] % config.window_width;
            uint8_t y = chip8->V[chip8->instr.Y] % config.window_height;
            uint8_t original_x = x;
            chip8->V[0x0F] = 0;
            for (uint8_t i = 0; i < chip8->instr.N; i++){
                x = original_x;
                uint8_t sprite_data = chip8->ram[chip8->I + i];
                for (int8_t j = 7; j >= 0; j--){
                    if (((sprite_data >> j) & 0x01) && chip8->display[y * config.window_width + x])
                    {
                        chip8->V[0x0F] = 1;
                    }
                    chip8->display[y * config.window_width + x] ^= (sprite_data>>j) & 0x01;   //  ()
                    if(++x > config.window_width){
                        break;
                    }
                }
                if(++y > config.window_height){
                    break;
                }
            }
            break;

        case 0x0E:
            if(chip8->instr.NN == 0x9E){
                if(chip8->keypad[chip8->V[chip8->instr.X]]){
                    chip8->pc += 2;
                }
            }else if(chip8->instr.NN == 0xA1){
                if(!chip8->keypad[chip8->V[chip8->instr.X]]){
                    chip8->pc += 2;
                }
            }
            break;

        case 0x0F:
            switch(chip8->instr.NN){
                case 0x07:
                    chip8->V[chip8->instr.X] = chip8->delay_timer;
                    break;

                case 0x0A:;
                    bool key_pressed = false;
                    for (uint8_t i = 0; i < sizeof(chip8->keypad); i++){
                        if(chip8->keypad[i]){
                            key_pressed = true;
                            chip8->V[chip8->instr.X] = chip8->keypad[i];
                            break;
                        }
                    }
                    if(!key_pressed){
                        chip8->pc -= 2;
                    }
                    break;

                case 0x15:
                    chip8->delay_timer = chip8->V[chip8->instr.X];
                    break;
                
                case 0x18:
                    chip8->sound_timer = chip8->V[chip8->instr.X];
                    break;

                case 0x1E:
                    chip8->I += chip8->V[chip8->instr.X];
                    break;
                
                case 0x29:
                    chip8->I = chip8->V[chip8->instr.X] * 5;
                    break;
                
                case 0x33:
                    //
                    break;
                
                case 0x55:
                    //
                    break;

                case 0x65:
                    //
                    break;

                default:
                    break;
            }
            break;

        default:
            break;
    }
}


int main(int argc, char** argv){
    (void)argc;
    (void)argv;

    srand(time(0));

    if(argc < 2){
        printf("Usage: %s <rom_name> <optional_flags>\n", argv[0]);
        printf("\nOptional flags:\n\t-d : Enable pixel outline\n");
        exit(1);
    }

    //Initialize emulator configs
    config_t config = {0};
    if(!set_config_from_args(&config, argc, argv)){
        printf("Error!");
        exit(1);
    }
    
    //Initialize Chip8
    chip8_t chip8 = {0};
    char *rom_name = argv[1];
    if(!init_chip8(&chip8, rom_name)){////
        exit(1);
    }

    //Initialize SDL
    sdl_t sdl = {0};
    if(!init_sdl(&sdl, config)){
        printf("Error!");
        exit(1);
    }

    //Initial Screen Clear
    clear_screen(sdl, config);

    //Main Loop
    while(chip8.state != QUIT){
        //handle user input
        handle_input(&chip8);

        if(chip8.state == PAUSED){
            continue;
        }
        
        // get time();
        // Emulate chip8 instructions
        emulate_instr(&chip8, config);
        //get time elapsed since lat gettime()
        //..
        // Delay for approx 60hz/60fps
        SDL_Delay(16);
        //SDL_Delay(16-time elapsed);
        //Update window with changes
        update_screen(sdl, chip8, config);
    }

    //Final cleanup
    finalcleanup(sdl);

    exit(0);
}