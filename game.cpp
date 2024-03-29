#include "precomp.h" // include (only) this in every .cpp file

constexpr auto num_tanks_blue = 2048;
constexpr auto num_tanks_red = 2048;

constexpr auto tank_max_health = 1000;
constexpr auto rocket_hit_value = 60;
constexpr auto particle_beam_hit_value = 50;

constexpr auto tank_max_speed = 1.0;

constexpr auto health_bar_width = 70;

constexpr auto max_frames = 2000;

//Global performance timer
constexpr auto REF_PERFORMANCE = 389333; //UPDATE THIS WITH YOUR REFERENCE PERFORMANCE (see console after 2k frames)
static timer perf_timer;
static float duration;

//Load sprite files and initialize sprites
static Surface* tank_red_img = new Surface("assets/Tank_Proj2.png");
static Surface* tank_blue_img = new Surface("assets/Tank_Blue_Proj2.png");
static Surface* rocket_red_img = new Surface("assets/Rocket_Proj2.png");
static Surface* rocket_blue_img = new Surface("assets/Rocket_Blue_Proj2.png");
static Surface* particle_beam_img = new Surface("assets/Particle_Beam.png");
static Surface* smoke_img = new Surface("assets/Smoke.png");
static Surface* explosion_img = new Surface("assets/Explosion.png");

static Sprite tank_red(tank_red_img, 12);
static Sprite tank_blue(tank_blue_img, 12);
static Sprite rocket_red(rocket_red_img, 12);
static Sprite rocket_blue(rocket_blue_img, 12);
static Sprite smoke(smoke_img, 4);
static Sprite explosion(explosion_img, 9);
static Sprite particle_beam_sprite(particle_beam_img, 3);
std::mutex mutex_tanks;
std::mutex mutex_rockets;
const static vec2 tank_size(7, 9);
ThreadPool pool;
static uint8_t tank_radius = 3;
static uint8_t rocket_radius = 5;

// -----------------------------------------------------------
// Initialize the simulation state
// This function does not count for the performance multiplier
// (Feel free to optimize anyway though ;) )
// -----------------------------------------------------------
void Game::init()
{
    frame_count_font = new Font("assets/digital_small.png", "ABCDEFGHIJKLMNOPQRSTUVWXYZ:?!=-0123456789.");

    tanks.reserve(num_tanks_blue + num_tanks_red);
    constexpr uint max_rows = 24;

    const float start_blue_x = tank_size.x + 40.0f;
    const float start_blue_y = tank_size.y + 30.0f;

    const float start_red_y = tank_size.y + 30.0f;

    constexpr float spacing = 7.5f;

    //Spawn blue tanks
    for (int i = 0; i < num_tanks_blue; i++)
    {
        const vec2 position{start_blue_x + ((i % max_rows) * spacing), start_blue_y + ((i / max_rows) * spacing)};
        tanks.emplace_back(position.x, position.y, BLUE, &tank_blue, &smoke, 1100.f, position.y + 16, tank_max_health,
                           tank_max_speed);
    }
    //Spawn red tanks
    for (int i = 0; i < num_tanks_red; i++)
    {
        constexpr float start_red_x = 1088.0f;
        const vec2 position{start_red_x + ((i % max_rows) * spacing), start_red_y + ((i / max_rows) * spacing)};
        tanks.emplace_back(position.x, position.y, RED, &tank_red, &smoke, 100.f, position.y + 16, tank_max_health,
                           tank_max_speed);
    }

    particle_beams.emplace_back(vec2(590, 327), vec2(100, 50), &particle_beam_sprite, particle_beam_hit_value);
    particle_beams.emplace_back(vec2(64, 64), vec2(100, 50), &particle_beam_sprite, particle_beam_hit_value);
    particle_beams.emplace_back(vec2(1200, 600), vec2(100, 50), &particle_beam_sprite, particle_beam_hit_value);
}


void Game::update_tanks_multithreaded()
{
    int portion = tanks.size() / pool.get_thread_count();
    int remainder = tanks.size() % pool.get_thread_count();
    int end = 0;
    std::vector<std::future<void>> futures;
    for (int i = 0; i < pool.get_thread_count(); i++)
    {
        int start = end;
        end += portion;
        if (remainder > 0)
        {
            end++;
            remainder--;
        }

        pool.mutex_available_threads.lock();
        if (pool.threads_available())
        {
            futures.push_back(pool.enqueue([&, start, end]() { update_tanks_partial(start, end); }));
            pool.mutex_available_threads.unlock();
        }
        else
        {
            pool.mutex_available_threads.unlock();
            update_tanks_partial(start, end);
        }
    }

    for (int c = 0; c < futures.size(); c++)
    {
        futures.at(c).wait();
    }
}


void Game::update_tanks_partial(int start, int end)
{
    for (int c = start; c < end; c++)
    {
        Tank& tank = tanks.at(c);
        if (tank.active)
        {
            {
                // prevent access violations to the tank list, lock_guard unlocks when out of scope
                const std::lock_guard<std::mutex> guard_tank(mutex_tanks);
                //Move tanks according to speed and nudges (see above) also reload
                tank.tick(background_terrain);
            }
            //Shoot at closest target if reloaded
            if (tank.rocket_reloaded())
            {
                Tank& target = find_closest_enemy(tank);
                {
                    // prevent access violations, lock_guard unlocks when out of scope
                    const std::lock_guard<std::mutex> guard_rockets(mutex_rockets);
                    rockets.push_back(
                        Rocket(tank.position, (target.get_position() - tank.position).normalized() * 3,
                               rocket_radius,
                               tank.allignment, ((tank.allignment == RED) ? &rocket_red : &rocket_blue)));
                }
                tank.reload_rocket();
            }
        }
    }
}

// -----------------------------------------------------------
// Close down application
// -----------------------------------------------------------
void Game::shutdown()
{
}

// -----------------------------------------------------------
// Iterates through all tanks and returns the closest enemy tank for the given tank
// -----------------------------------------------------------
Tank& Game::find_closest_enemy(const Tank& current_tank)
{
    float closest_distance = numeric_limits<float>::infinity();
    int closest_index = 0;

    for (int i = 0; i < tanks.size(); i++)
    {
        if (tanks.at(i).allignment != current_tank.allignment && tanks.at(i).active)
        {
            if (const float sqr_dist = fabsf((tanks.at(i).get_position() - current_tank.get_position()).dot());
                sqr_dist < closest_distance)
            {
                closest_distance = sqr_dist;
                closest_index = i;
            }
        }
    }

    return tanks.at(closest_index);
}

/**
 * \brief sorts a vector with predicate
 * \tparam T Type within vector
 * \tparam Function Predicate Function type
 * \param original original vector
 * \param begin begin merge sort (usually 0)
 * \param end end merge sort
 * \param predicate function that takes two pointer arguments
 * \return sorted vector
 */
template <typename T, typename Function>
std::vector<T*> Tmpl8::Game::merge_sort(std::vector<T>& original, const int begin,
                                        const int end, const Function predicate)
{
    if (const int num_tanks = end - begin; num_tanks < 2) //stop condition
        return std::vector<T*>{&original.at(begin)};


    const int mid = (begin + end) / 2;
    std::vector<T*> left;
    std::vector<T*> right;
    pool.mutex_available_threads.lock();
    if (pool.threads_available())
    {
        auto task = pool.enqueue([&original, mid, end, predicate]
        {
            return merge_sort(original, mid, end, predicate);
        });
        pool.mutex_available_threads.unlock();
        left = merge_sort(original, begin, mid, predicate);
        right = task.get();
    }
    else
    {
        pool.mutex_available_threads.unlock();
        left = merge_sort<T, Function>(original, begin, mid, predicate);
        right = merge_sort<T, Function>(original, mid, end, predicate);
    }
    return merge<T, Function>(left, right, predicate);
}

template <typename T, typename Function>
std::vector<T*> Tmpl8::Game::merge(std::vector<T*> left, std::vector<T*> right,
                                   const Function predicate)
{
    std::vector<T*> result_vector;
    result_vector.reserve(left.size() + right.size());

    while (!left.empty() && !right.empty())
    {
        if (predicate(left[0], right[0]))
        {
            result_vector.emplace_back(left[0]);
            left.erase(left.begin());
            left.shrink_to_fit();
        }
        else
        {
            result_vector.emplace_back(right[0]);
            right.erase(right.begin());
            right.shrink_to_fit();
        }
    }
    while (!left.empty() && right.empty())
    {
        result_vector.emplace_back(left[0]);
        left.erase(left.begin());

        left.shrink_to_fit();
    }
    while (left.empty() && !right.empty())
    {
        result_vector.emplace_back(right[0]);
        right.erase(right.begin());

        right.shrink_to_fit();
    }

    return result_vector;
}

//Checks if a point lies on the left of an arbitrary angled line
bool Game::left_of_line(const vec2 line_start, const vec2 line_end, const vec2 point)
{
    return ((line_end.x - line_start.x) * (point.y - line_start.y) - (line_end.y - line_start.y) * (point.x - line_start
        .x)) < 0;
}

void Game::collision()
{
    //Check tank collision and nudge tanks away from each other
    for (Tank& tank : tanks)
    {
        if (tank.active)
        {
            for (Tank& other_tank : tanks)
            {
                if (&tank == &other_tank || !other_tank.active) continue; //if tank is the same or dead continue

                vec2 dir = tank.get_position() - other_tank.get_position();
                const float dir_squared_len = dir.dot();

                uint8_t col_squared_len = tank_radius << 1;
                col_squared_len *= col_squared_len;

                if (dir_squared_len < col_squared_len)
                {
                    tank.push(dir.normalized(), 1.f);
                }
            }
        }
    }
}


void Game::update_tanks()
{
    //Update tanks
    for (Tank& tank : tanks)
    {
        if (tank.active)
        {
            //Move tanks according to speed and nudges (see above) also reload
            tank.tick(background_terrain);

            //Shoot at closest target if reloaded
            if (tank.rocket_reloaded())
            {
                Tank& target = find_closest_enemy(tank);

                rockets.emplace_back(tank.position, (target.get_position() - tank.position).normalized() * 3,
                                     rocket_radius, tank.allignment,
                                     tank.allignment == RED ? &rocket_red : &rocket_blue);

                tank.reload_rocket();
            }
        }
    }
}

void Game::find_first_active_tank(uint16_t& first_active) const
{
    //Find first active tank (this loop is a bit disgusting, fix?)
    for (const Tank& tank : tanks)
    {
        if (tank.active)
            break;

        first_active++;
    }
}


bool static convex_hull_pred(const Tank* t1, const Tank* t2)
{
    return t1->position.x < t2->position.x;
}

void Game::convex_hull()
{
    const vector<Tank*> sorted_tanks = merge_sort(tanks, 0, tanks.size(), convex_hull_pred);

    //upper hull
    std::vector<vec2> upper_hull;
    for (int i = 0; i < sorted_tanks.size(); ++i)
    {
        const Tank* tank = sorted_tanks[i];

        if (!tank->active) continue;

        while (upper_hull.size() >= 2 && left_of_line(
            upper_hull[upper_hull.size() - 2], upper_hull.back(), tank->position))
        {
            upper_hull.pop_back();
        }

        upper_hull.push_back(tank->position);
    }


    //lower hull
    std::vector<vec2> lower_hull;
    for (int i = sorted_tanks.size() - 1; i >= 0; --i)
    {
        const Tank* tank = sorted_tanks[i];

        if (!tank->active) continue;

        while (lower_hull.size() >= 2 && left_of_line(
            lower_hull[lower_hull.size() - 2], lower_hull.back(), tank->position))
            lower_hull.pop_back();


        lower_hull.push_back(tank->position);
    }

    forcefield_hull.insert(forcefield_hull.end(), upper_hull.begin(), upper_hull.end());
    forcefield_hull.insert(forcefield_hull.end(), lower_hull.begin(), lower_hull.end());
}

void Game::update_rocket()
{
    //Update rockets

    vector<future<void>> futures{};
    auto portion = rockets.size() / pool.get_thread_count();
    auto remainder = rockets.size() % pool.get_thread_count();
    int end = 0;

    for (int i = 0; i < pool.get_thread_count(); i++)
    {
        int start = end;
        end += portion;
        if (remainder > 0)
        {
            end++;
            remainder--;
        }

        pool.mutex_available_threads.lock();
        if (pool.threads_available())
        {
            futures.push_back(pool.enqueue([&, start, end]()
            {
                 update_rockets_partial(start, end);
            }));
            
            pool.mutex_available_threads.unlock();
        }
        else
        {
            pool.mutex_available_threads.unlock();
            update_rockets_partial(start, end);
        }
    }
    for (auto& future : futures)
        future.wait();
    
}

void Game::update_rockets_partial(const int start, const int end)
{
    for (int j = start; j < end; j++)
    {
        Rocket& rocket = rockets[j];
        {
            const std::lock_guard<std::mutex> gaurd_rocket(mutex_rockets);
            rocket.tick();
        }
        //Check if rocket collides with enemy tank, spawn explosion, and if tank is destroyed spawn a smoke plume
        for (Tank& tank : tanks)
        {
            if (tank.active && (tank.allignment != rocket.allignment) && rocket.intersects(
                tank.position, tank_radius))
            {
                const std::lock_guard<std::mutex> guard_tank(mutex_tanks);
                explosions.emplace_back(&explosion, tank.position);

                if (tank.hit(rocket_hit_value))
                    smokes.emplace_back(smoke, tank.position - vec2(7, 24));


                rocket.active = false;
                break;
            }
        }
    }
}

void Game::rocket_hits_convex()
{
    //Disable rockets if they collide with the "forcefield"
    //Hint: A point to convex hull intersection test might be better here? :) (Disable if outside)
    for (Rocket& rocket : rockets)
    {
        if (rocket.active)
        {
            for (size_t i = 0; i < forcefield_hull.size(); i++)
            {
                if (circle_segment_intersect(forcefield_hull.at(i),
                                             forcefield_hull.at((i + 1) % forcefield_hull.size()), rocket.position,
                                             rocket.collision_radius))
                {
                    explosions.emplace_back(&explosion, rocket.position);
                    rocket.active = false;
                }
            }
        }
    }
}

void Game::update_particle_beams()
{
    //Update particle beams
    for (Particle_beam& particle_beam : particle_beams)
    {
        particle_beam.tick(tanks);

        //Damage all tanks within the damage window of the beam (the window is an axis-aligned bounding box)
        for (Tank& tank : tanks)
        {
            if (tank.active && particle_beam.rectangle.intersects_circle(
                tank.get_position(), tank_radius) && tank.hit(particle_beam.damage))
                smokes.emplace_back(smoke, tank.position - vec2(0, 48));
        }
    }
}

// -----------------------------------------------------------
// Update the game state:
// Move all objects
// Update sprite frames
// Collision detection
// Targeting etc..
// -----------------------------------------------------------
void Game::update()
{
    // collision_tanks(tanks, 0);


    //Calculate the route to the destination for each tank using A*
    //Initializing routes here so it gets counted for performance..
    if (frame_count == 0)
    {
        calculate_route_multithreaded(tanks);
    }


    collision();
    update_tanks_multithreaded();

    //Update smoke plumes
    for (Smoke& smoke : smokes)
        smoke.tick();


    //Calculate "forcefield" around active tanks
    forcefield_hull.clear();

    //calculate convex hull
    convex_hull();

    //update rocket
    update_rocket();

    if (frame_count == 1)
    {
        printf("end");
    }

    //rocket hits convex
    rocket_hits_convex();

    //Remove exploded rockets with remove erase idiom
    rockets.erase(std::remove_if(rockets.begin(), rockets.end(), [](const Rocket& rocket) { return !rocket.active; }),
                  rockets.end());

    //update particle beams
    update_particle_beams();

    //Update explosion sprites
    for (Explosion& explosion : explosions)
        explosion.tick();

    //remove when done with remove erase idiom
    explosions.erase(std::remove_if(explosions.begin(), explosions.end(),
                                    [](const Explosion& explosion) { return explosion.done(); }), explosions.end());
}

static bool tank_merge_sort_pred(const Tank* t1, const Tank* t2)
{
    return t1->compare_health(*t2) <= 0;
}

// -----------------------------------------------------------
// Draw all sprites to the screen
// (It is not recommended to multi-thread this function)
// -----------------------------------------------------------
void Game::draw()
{
    // clear the graphics window
    screen->clear(0);

    //Draw background
    background_terrain.draw(screen);

    //Draw sprites
    for (int i = 0; i < num_tanks_blue + num_tanks_red; i++)
        tanks.at(i).draw(screen);

    for (Rocket& rocket : rockets)
        rocket.draw(screen);


    for (Smoke& smoke : smokes)
        smoke.draw(screen);

    for (Particle_beam& particle_beam : particle_beams)
        particle_beam.draw(screen);

    for (Explosion& explosion : explosions)
        explosion.draw(screen);

    //Draw forcefield (mostly for debugging, its kinda ugly..)
    for (size_t i = 0; i < forcefield_hull.size(); i++)
    {
        vec2 line_start = forcefield_hull.at(i);
        vec2 line_end = forcefield_hull.at((i + 1) % forcefield_hull.size());
        line_start.x += HEALTHBAR_OFFSET;
        line_end.x += HEALTHBAR_OFFSET;
        screen->line(line_start, line_end, 0x0000ff);
    }

    //Draw sorted health bars
    for (int t = 0; t < 2; t++)
    {
        const int num_tanks = ((t < 1) ? num_tanks_blue : num_tanks_red);

        const int begin = ((t < 1) ? 0 : num_tanks_blue);
        std::vector<Tank*> sorted_tanks = merge_sort<Tank>(
            tanks, begin, begin + num_tanks, tank_merge_sort_pred);

        sorted_tanks.erase(std::remove_if(sorted_tanks.begin(), sorted_tanks.end(),
                                          [](const Tank* tank) { return !tank->active; }), sorted_tanks.end());

        draw_health_bars(sorted_tanks, t);
    }
}

void Game::calculate_route_multithreaded(vector<Tank>& t)
{
    int portion = tanks.size() / pool.get_thread_count();
    std::vector<std::future<void>> futures;
    for (size_t i = 0; i < pool.get_thread_count(); i++)
    {
        pool.mutex_available_threads.lock();
        if (pool.threads_available())
        {
            futures.push_back(pool.enqueue([&t, i, portion] { calc_route_singlethread(t, i, portion); }));
            pool.mutex_available_threads.unlock();
        }
        else
        {
            pool.mutex_available_threads.unlock();
            calc_route_singlethread(tanks, i, portion);
        }
    }

    for (auto& f : futures)
    {
        f.wait();
    }
}


//position is the portion it has to calculate
void Tmpl8::Game::calc_route_singlethread(vector<Tank>& tanks, const int& position, const int& portion)
{
    Terrain terrain;
    const int start = position * portion;
    for (size_t i = 0; i < portion; i++)
    {
        tanks[start + i].set_route(terrain.a_star(tanks[start + i], tanks[start + i].target));
    }
}

// -----------------------------------------------------------
// Sort tanks by health value using insertion sort
// -----------------------------------------------------------
void Tmpl8::Game::insertion_sort_tanks_health(const std::vector<Tank>& original, std::vector<const Tank*>& sorted_tanks,
                                              int begin, int end)
{
    const int NUM_TANKS = end - begin;
    sorted_tanks.reserve(NUM_TANKS);
    sorted_tanks.emplace_back(&original.at(begin));

    for (int i = begin + 1; i < (begin + NUM_TANKS); i++)
    {
        const Tank& current_tank = original.at(i);

        for (int s = (int)sorted_tanks.size() - 1; s >= 0; s--)
        {
            if (const Tank* current_checking_tank = sorted_tanks.at(s); (current_checking_tank->
                compare_health(current_tank) <= 0))
            {
                sorted_tanks.insert(1 + sorted_tanks.begin() + s, &current_tank);
                break;
            }

            if (s == 0)
            {
                sorted_tanks.insert(sorted_tanks.begin(), &current_tank);
                break;
            }
        }
    }
}

// -----------------------------------------------------------
// Draw the health bars based on the given tanks health values
// -----------------------------------------------------------
void Tmpl8::Game::draw_health_bars(const std::vector<Tank*>& sorted_tanks, const int team) const
{
    const int health_bar_start_x = (team < 1) ? 0 : (SCRWIDTH - HEALTHBAR_OFFSET) - 1;
    const int health_bar_end_x = (team < 1) ? health_bar_width : health_bar_start_x + health_bar_width - 1;

    for (int i = 0; i < SCRHEIGHT - 1; i++)
    {
        //Health bars are 1 pixel each
        const int health_bar_start_y = i * 1;
        const int health_bar_end_y = health_bar_start_y + 1;

        screen->bar(health_bar_start_x, health_bar_start_y, health_bar_end_x, health_bar_end_y, REDMASK);
    }

    //Draw the <SCRHEIGHT> least healthy tank health bars
    const int draw_count = std::min(SCRHEIGHT, (int)sorted_tanks.size());
    for (int i = 0; i < draw_count - 1; i++)
    {
        //Health bars are 1 pixel each
        const int health_bar_start_y = i * 1;
        const int health_bar_end_y = health_bar_start_y + 1;

        const float health_fraction = (1 - ((double)sorted_tanks[i]->health / (double)tank_max_health));

        if (team == 0)
        {
            screen->bar(health_bar_start_x + (int)((double)health_bar_width * health_fraction), health_bar_start_y,
                        health_bar_end_x, health_bar_end_y, GREENMASK);
        }
        else
        {
            screen->bar(health_bar_start_x, health_bar_start_y,
                        health_bar_end_x - (int)((double)health_bar_width * health_fraction), health_bar_end_y,
                        GREENMASK);
        }
    }
}

// -----------------------------------------------------------
// When we reach max_frames print the duration and speedup multiplier
// Updating REF_PERFORMANCE at the top of this file with the value
// on your machine gives you an idea of the speedup your optimizations give
// -----------------------------------------------------------
void Tmpl8::Game::measure_performance()
{
    if (frame_count >= max_frames)
    {
        if (!lock_update)
        {
            duration = perf_timer.elapsed();
            cout << "Duration was: " << duration << " (Replace REF_PERFORMANCE with this value)" << endl;
            lock_update = true;
        }

        frame_count--;
    }

    if (lock_update)
    {
        char buffer[128];
        screen->bar(420 + HEALTHBAR_OFFSET, 170, 870 + HEALTHBAR_OFFSET, 430, 0x030000);
        int ms = (int)duration % 1000, sec = ((int)duration / 1000) % 60, min = ((int)duration / 60000);
        sprintf(buffer, "%02i:%02i:%03i", min, sec, ms);
        frame_count_font->centre(screen, buffer, 200);
        sprintf(buffer, "SPEEDUP: %4.1f", REF_PERFORMANCE / duration);
        frame_count_font->centre(screen, buffer, 340);
    }
}

// -----------------------------------------------------------
// Main application tick function
// -----------------------------------------------------------
void Game::tick()
{
    if (!lock_update)
    {
        update();
    }
    draw();

    measure_performance();

    // print something in the graphics window
    //screen->Print("hello world", 2, 2, 0xffffff);

    // print something to the text window
    //cout << "This goes to the console window." << std::endl;

    //Print frame count
    frame_count++;
    const string frame_count_string = "FRAME: " + std::to_string(frame_count);
    frame_count_font->print(screen, frame_count_string.c_str(), 350, 580);
}
