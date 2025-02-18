#include "doom.h"


static void player_movement(game_state *g_)
{
    SDL_Event event;
    SDL_PollEvent(&event);
    switch (event.type)
    {
        case SDL_QUIT:
	    g_->quit = true;
	    break;
	case SDL_KEYDOWN:
            if(event.key.keysym.sym >= 'a' && event.key.keysym.sym <= 'z')
            {
                g_->pressed_keys[event.key.keysym.sym - 'a'] = 1; 
            }
            if(event.key.keysym.sym == SDLK_ESCAPE)
	        g_->quit = true;
           if(event.key.keysym.sym == SDLK_m)
	        g_->render_mode = !(g_->render_mode);
            break;
        case SDL_KEYUP:
            if(event.key.keysym.sym >= 'a' && event.key.keysym.sym <= 'z')
            {
                g_->pressed_keys[event.key.keysym.sym - 'a'] = 0; 
            }           
            break;
    } 
    for(short int i = 0; i < 26; i ++)
    {
        if(g_->pressed_keys[i] == 1)
        {
            // q z d s
            if(i == 16 || i == 25 || i == 3 || i == 18)
            {
                float s = 0.0f;
                if(i == 16) s = -90.0f;
                if(i == 3) s = 90.0f;
                if(i == 18) s = -180.0f;
                float d_x = cos(DEG2RAD((double)(g_->p.rotation  + s)));
                float d_y = sin(DEG2RAD((double)(g_->p.rotation  + s))); 
                g_->p.pos.y += 2.0f * d_y;
                g_->p.pos.x += 2.0f * d_x;
            }
            // a
            if(i == 0)
                g_->p.rotation -= 1;
            // e
            if(i == 4)
                g_->p.rotation += 1;
        }
    } 
    if(g_->p.pos.x < 0) g_->p.pos.x = 0;
    if(g_->p.pos.y < 0) g_->p.pos.y = 0;
    if(g_->p.pos.x >= (float)(WINDOW_WIDTH - 6)) g_->p.pos.x = WINDOW_WIDTH - 6.0f;
    if(g_->p.pos.y >= (float)(WINDOW_HEIGHT - 6)) g_->p.pos.y = WINDOW_HEIGHT - 6.0f;
    if(g_->p.rotation > 360.0f) g_->p.rotation = 0.0f;
    if(g_->p.rotation < -360.0f) g_->p.rotation = 0.0f;
    //printf("p->rot: %f° \n", g_->p.rotation);
}

static void set_pixel_color(game_state *g_, int x, int y, int c)
{
    // prevents segfaults
    if(x < 0 || y < 0 || x > WINDOW_WIDTH || y > WINDOW_HEIGHT)
        return;
    int ix = (WINDOW_WIDTH * y) + x;
    g_->buffer[ix] = (c);
}

// bresenham's algorithm
static void d_line(game_state *g_, int x0, int y0, int x1, int y1, uint32_t color)
{
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1)
    {
        set_pixel_color(g_, x0, y0, color);

        if (x0 == x1 && y0 == y1) {
            break; // complete line
        }

        int e2 = err * 2;
        if (e2 > -dy)
        {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

static void d_rect(game_state *g_, int x, int y, int width, int height)
{
    if((x < 0) || (y < 0) ||(x + width) > WINDOW_WIDTH || (y + height) > WINDOW_HEIGHT)
        return;
    /*
    nah bro
    SDL_Rect rect = { x, y, width, height};
    SDL_SetRenderDrawColor(g_->renderer, 0, 0, 255, 255);

    SDL_RenderFillRect(g_->renderer, &rect);
    */
    for(uint32_t i = 0; i < width; i ++ )
    {
        d_line(g_,
            x + i, y,
            x + i, y + height,
            0xFF00FF
        );
    }
}


// rotate vector v by angle a (in radians)
static inline v2 rotate_v2(v2 v, f32 a)
{
    return (v2){
        (v.x * cos(a)) - (v.y * sin(a)),
        (v.x * sin(a)) + (v.y * cos(a))
    };
}

// intersection of two segments
// https://en.wikipedia.org/wiki/Line-line_intersection
static inline v2 intersect_segments(v2 a0, v2 a1, v2 b0, v2 b1)
{
    const f32 d =
        ((a0.x - a1.x) * (b0.y - b1.y))
            - ((a0.y - a1.y) * (b0.x - b1.x));

    if(fabsf(d) < 0.000001f) 
        return (v2) {NAN, NAN};

    const f32 
        t = ((a0.x - b0.x) * (b0.y - b1.y))
            - ((a0.y - b0.y) * (b0.x - b1.x)) / d,
        u = ((a0.x - b0.x) * (a0.y - a1.y))
            - ((a0.y - b0.y) * (a0.x - a1.x)) / d;

    return (t >= 0 && t <= 1 && u >= 0 && u <= 1) ? 
        (v2){
            a0.x + (t * (a1.x - a0.x)),
            a0.y + (t * (a1.y - a0.y))
        } : (v2) {NAN, NAN};
}

// convert angle to [-(FOV/2)...+(FOV/2)] => [x in 0..WINDOW_WIDTH] 
static inline int screen_angle_to_x(f32 a)
{
    const f32 a_pi4 = (
            ((a + HFOV / 2.0f)
                / 
                HFOV
            ) * PI_2)
            + (PI_2 / 2.0f);
    return (int) ((WINDOW_WIDTH / 2) * (1.0f - tan(a_pi4)));
}

// 2D COORDINATES (world space)
// -> 3D COORDAINTES (camera space)
static inline v2 world_to_camera(game_state *g_, v2 p)
{
    // 1. translate
    const v2 t = (v2){
        p.x - g_->p.pos.x, 
        p.y - g_->p.pos.y
    };
    // 2. rotate
    return (v2)
        {
            t.x * (sin(DEG2RAD(g_->p.rotation))) - t.y * (cos(DEG2RAD(g_->p.rotation))),
            t.x * (cos(DEG2RAD(g_->p.rotation))) + t.y * (sin(DEG2RAD(g_->p.rotation)))
        };
}

// (radian)
static inline float normalize_angle(double angle)
{
    return fmod((angle + M_PI), 2 * M_PI) - M_PI;
}

// 2D Coordinates to 3D
// 3. Wall-Cliping (test if lines intersects with player viewcone/viewport by using vector2 cross product)
// 4. Perspective transformation  (scaling lines according to their dst from player, for appearance of perspective (far-away objects are smaller).
// 5. [COMING SOON ? ] Z-buffering (track depth of pixel to avoid rendering useless surfaces)
static void render(game_state *g_)
{
    // transform world view to 1st-person view   
    if(g_->render_mode)
    {
        for(uint32_t i = 0; i < g_->scene._walls_ix; i ++)
        {     
            const wall *l = &(g_->scene._walls[i]);
            float r = DEG2RAD(g_->p.rotation);
            const v2
                zdl = rotate_v2((v2){ 0.0f, 1.0f}, + (HFOV / 2.0f)),
                zdr = rotate_v2((v2){ 0.0f, 1.0f}, - (HFOV / 2.0f)),
                znl = (v2){ zdl.x * ZNEAR, zdl.y * ZNEAR },
                znr = (v2){ zdr.x * ZNEAR, zdr.y * ZNEAR },
                zfl = (v2){0.0f, 1.0f},
                zfr = rotate_v2((v2){ 0.0f, 1.0f}, + (HFOV / 2.0f));

            // line relative to player
            const v2 
                op0 = (v2)(world_to_camera(g_, l->a)),
                op1 = (v2)(world_to_camera(g_, l->b));
            
            // compute cliped positions
            v2 cp0 = op0, cp1 = op1;

            // both are behind player -> wall behind camera
            if(cp0.y <= 0 && cp0.x <= 0){
                continue;
            }

            // angle-clip against view frustum
            f32 
                ac0 = normalize_angle(atan2(cp0.y, cp0.x) - PI_2),
                ac1 = normalize_angle(atan2(cp1.y, cp0.x) - PI_2);

            // clip against frustum (if wall intersects with clip planes)
            if(cp0.y < ZNEAR
                || cp1.y < ZNEAR
                || ac0 > +(HFOV / 2)
                || ac0 > -(HFOV / 2)
            ){
                // do smth

            }
            // d_line(g_, a_.x, a_.y, b_.x, b_.y, 0xFF00FF);
        }
    }
    else
    {
        // world-view
        // lines
        for(uint32_t i = 0; i < g_->scene._walls_ix; i ++)
        {
            d_line(g_, 
                g_->scene._walls[i].a.x, g_->scene._walls[i].a.y, 
                g_->scene._walls[i].b.x, g_->scene._walls[i].b.y,
                0xFFFFFF
            ); 
        }
        // player square and line 
        for(uint32_t i = 0; i < FOV; i ++)
        {
            d_line(g_, g_->p.pos.x + 3, g_->p.pos.y + 3, 
               g_->p.pos.x + 3 + (200.0f * cos(DEG2RAD((float)(g_->p.rotation - (FOV / 2) + i)))),
               g_->p.pos.y + 3 + (200.0f * sin(DEG2RAD((float)(g_->p.rotation - (FOV / 2) + i)))),
               0xFFFFFF
            );        
        }
        d_line(g_, g_->p.pos.x + 3, g_->p.pos.y + 3, 
           g_->p.pos.x + 3 + (50.0f * cos(DEG2RAD(g_->p.rotation))),
           g_->p.pos.y + 3 + (50.0f * sin(DEG2RAD(g_->p.rotation))),
           0xFFFF00
        );        
        d_rect(g_, g_->p.pos.x, g_->p.pos.y, 5, 5); 
    }
}

static void scene(game_state *g_)
{ 
    g_->scene._walls = (wall *) malloc(100 * sizeof(wall));
    if (!g_->scene._walls)
    {

    }
    wall *l = &(g_->scene._walls[0]);

    l->a.x = WINDOW_WIDTH / 2;  l->b.x = WINDOW_WIDTH/2;
    l->a.y = 100;               l->b.y = 300;
    l->color = 0xFF00FF;

    l = &(g_->scene._walls[1]);
    l->a.x = WINDOW_WIDTH / 2; l->b.x = WINDOW_WIDTH/2 + 300;
    l->a.y = 100;              l->b.y = 100;
    l->color = 0xFFFF00;    
    g_->scene._walls_ix = 2;
}

static void multiThread_present(game_state *g_)
{
    // cpu pixel buffer => locked sdl texture 
    // [hardware acceleration?] => video memory alloction?
    void *px;
    int pitch;
    // lock texture before writing to it (SDL uses mutex and multithread for textures rendering)
    SDL_LockTexture(g_->texture, NULL, &px, &pitch); 
    // multi-threaded loop operations for mempcpying 
    // game_state.buffer ==> into SDL texture 
    #pragma omp parallel for
    {
        for(size_t u = 0; u < WINDOW_HEIGHT; u ++)
        {
	    memcpy(
	    	&((uint8_t*) px)[u * pitch],
                &g_->buffer[u * WINDOW_WIDTH],
                WINDOW_WIDTH * (sizeof(int))
             );
	}
    }
    SDL_UnlockTexture(g_->texture);

    SDL_SetRenderTarget(g_->renderer, NULL);
    SDL_SetRenderDrawColor(g_->renderer, 0, 0, 0, 0xFF);
    SDL_SetRenderDrawBlendMode(g_->renderer, SDL_BLENDMODE_NONE);

    SDL_RenderClear(g_->renderer);
    SDL_RenderCopy(g_->renderer, g_->texture, NULL, NULL);
    SDL_RenderPresent(g_->renderer);	
}


static void destroy_window(game_state *g_)
{
    SDL_DestroyTexture(g_->texture);
    SDL_DestroyRenderer(g_->renderer);
    SDL_DestroyWindow(g_->window);
    SDL_Quit();
 
    free(g_->buffer);
    // free(g_->lines);
    free(g_);
    return;
}

int init_doom(game_state **g_)
{  
    *g_ =  malloc(1 * sizeof(game_state));
    if (!*g_)
    {
        fprintf(stderr, "Error allocating GAME_STATE. \n");
        return (-1);
    }
    if (SDL_Init(SDL_INIT_EVERYTHING) != 0)
    {
        fprintf(stderr, "Error initializing SDL. \n");
        return -1;
    }
    (*g_)->window = SDL_CreateWindow(
        "doom.c",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH,
        WINDOW_HEIGHT, 0
    );
    if(!(*g_)->window)
    {
        fprintf(stderr, "Error creating SDL Window. \n");
        return -1;
    }
    (*g_)->renderer = SDL_CreateRenderer(
        (*g_)->window, -1, 0 
    );
    if(!(*g_)->renderer) {
        fprintf(stderr, "Error creating SDL Renderer. \n");
        return -1;
    }   

    // screen size texture
    (*g_)->texture =
      SDL_CreateTexture(
            (*g_)->renderer,
            SDL_PIXELFORMAT_ABGR8888,
            SDL_TEXTUREACCESS_STREAMING,
            WINDOW_WIDTH,
            WINDOW_HEIGHT
        );

    // initializing pixel buffer
    (*g_)->buffer = malloc(
            (WINDOW_WIDTH * WINDOW_HEIGHT
                * sizeof(int))
            );
    if (!(*g_)->buffer)
    {
        fprintf(stderr, "Error allocating game_buffer. \n");
        return (-1);
    }

    // screen size texture
    (*g_)->texture =
      SDL_CreateTexture(
         (*g_)->renderer,
         SDL_PIXELFORMAT_ABGR8888,
         SDL_TEXTUREACCESS_STREAMING,
         WINDOW_WIDTH,
         WINDOW_HEIGHT
    );
    if(!(*g_)->texture)
    {
        fprintf(stderr, "Error creating texture. \n");
        return (-1);
    }
    printf("Initialization successful. \n");
    (*g_)->quit = false;
    (*g_)->p.pos.x = WINDOW_WIDTH / 3;
    (*g_)->p.pos.y = WINDOW_HEIGHT / 3;
    (*g_)->p.rotation = 0;
    (*g_)->render_mode = false;
    return 0;
}

static void update(game_state *g_)
{
    // reset buffer pixels to black
    memset(g_->buffer, BLACK, WINDOW_WIDTH * WINDOW_HEIGHT * 4);
   
    // uint32_t timeout = SDL_GetTicks() + FRAME_TARGET_TIME;
    uint32_t time_toWait = FRAME_TARGET_TIME - (SDL_GetTicks() - g_->l_frameTime );
    // lock at frame target time
    // by comparing ticks 
    if(time_toWait > 0 && time_toWait <= FRAME_TARGET_TIME)
    {
	SDL_Delay(time_toWait);
    }  
    g_->deltaTime  = (SDL_GetTicks() -  g_->l_frameTime) / 1000.0f;
    g_->l_frameTime = SDL_GetTicks();	
    render(g_);
    multiThread_present(g_);   

}


int main(int argc, char **argv)
{
    game_state  *g_ = NULL;
   
    if(init_doom(&g_) == -1)
    {
        destroy_window(g_);
        return (EXIT_FAILURE);
    }
    scene(g_);
    printf("Game is runnning.. [%dx%d]\n", WINDOW_WIDTH, WINDOW_HEIGHT);
    while(!(g_->quit))
    {
        player_movement(g_);
        update(g_);
        //
    }
    destroy_window(g_);
    return (EXIT_SUCCESS); 
}
