# Rawrotic's Predictive Input Buffer & Adaptive Auto Bot

Press **DEL** to open the menu.

## Predictive Input Buffer
- If you click a little too early, it waits and puts your click in at the moment it would work, so you don't die
- It does the same for releases: let go too early and it waits for the right moment
- In ship and wave, it times your holds and releases for you
- In wave, it lets go for you if holding any longer would crash you
- You pick how much help you want: 2, 4, 8 or 16 ticks (32 and 64 for testing)

## Show Trajectory
- Draws one line for where you'll go if you hold, and another for if you let go
- Looks about 2 seconds ahead, even through portals
- Shows where hitting an orb would take you
- Puts a red mark where you'd die and outlines what kills you

## Adaptive Auto Bot
- Plays the level for you
- It doesn't use a recording or macro. It looks ahead and works the level out as it goes
- It picks the safest way through, not just any way that survives
- If it dies, it remembers and tries something different the next attempt
- Show Path draws the route it's planning to take
- It never saves progress or new bests, so your stats stay yours
- It's tuned so your FPS doesn't drop while it's thinking
