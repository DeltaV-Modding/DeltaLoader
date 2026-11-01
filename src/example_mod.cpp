/* Hello Reader! If you're reading this, it means that you are interested in our modding system! This is written by Shayans, 
(Pretty much the only thing I did for the DeltaLoader, my main job was the launcher and the site). Oh and I also wrote the STEX better space code for Argon spawning.
I'm pretty much at the beginners level of C++. This is why basically this entire repository was written by Malionaro.
So let's just start!
*/
#include "sw/mod.hpp"
#include "sw/tiles.hpp" // If you want custom tiles
#include "sw/start_tiles.hpp"

#include <string>
using std::string;

// Here we can define the name of our mod, this is especially interesting for the log.
SW_MOD("delta_example")

// If we want custom tiles, we should create a function that's called after the game is done setting the vanilla tiles:
static void on_tiles_placed(const sw::tiles::EventInfo& info, void* user) { // These parameters are wanted by EventCallback, but we don't need them
    for (const sw::tiles::Cell& cell : sw::tiles::cells()) { // So that our island doesn't spawn multiple times (I used to have a horrible fix for this)
        if (cell.name == "data/tiles/argon_island.xml"){return;}
    }

    string error; // If there's an error, it's written inside of this string
    if (!sw::tiles::place_random("argon_island.xml", 0, 0, 10, 15, error)) { // Second parameter: X-Center of where the tile radius is, Third: Y-Center of where the tile radius is, Fourth parameter: Minimum radius of where it can spawn
        sw::mod::log("customtile was not placed: %s", error.c_str()); // You need the %s to properly display the error
    }
    /*
    if (!sw::tiles::place("customtile.xml", 10, 10, error)) { // This would place a fixed tile at the coordinates 10.000; 10.000
        sw::mod::log("customtile was not placed: %s", error.c_str());
    }
    */
}
// This function is called on the start of your mod
void sw_mod_start() {
    // This is a simple log output.
    sw::mod::log("DeltaLoader mod loaded.");

    if (!sw::tiles::watch()) { // watch() from tiles.hpp has to be called to setup the tile management
        return;
    }

    if (!sw::start_tiles::watch()) { //Same thing goes for here, you need this to do this start "card" in the menu for a base
        return;
    }
    string error; // Hope you didn't forget what this was
    if (!sw::tiles::on(sw::tiles::Event::tiles_placed, &on_tiles_placed, nullptr, error)) { // This listens for the event that the game placed all of it's own tiles and then calls on_tiles_placed, and we don't need a user here.
        sw::mod::log("Something is wrong, can't listen for the tile placed event: %s", error.c_str()); // Because we love logging
        return;
    }

    sw::start_tiles::Base customBase; // We need a base object
    customBase.key = "argon_island"; // This grabs the tile xml and the map geometry, so set it to whatever your tile name is (without the file extension!)
    customBase.title = "Running Out Of Ideas"; // This is the display name of the base in the menu
    customBase.description = "Lorem Ipsum Dolor Sit Amet."; // This is the description of the base in the menu
    string errorBase; // Again, to write any errors inside this string if there are any
    if (!sw::start_tiles::add_base(customBase, errorBase)) {
        sw::mod::log("Could not add the base to the start things: %s", errorBase.c_str());
    }

    // There are other things I could explain now but I'm tired and many of the other things (like flags, menu icons and stuff like that) are broken right now
    // So you shouldn't use them anyway. (Except if you're a genius that's able to fix them)
    
    // But I will update this with future updates of the source code and this project in general.

    // Oh, and don't forget to update CMakeLists.txt!
}

// Called when you stop the game
void sw_mod_stop() {
    // Do whatever you want here, most of the time it just doesn't matter.
}