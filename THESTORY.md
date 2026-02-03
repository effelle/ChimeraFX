# Why build a custom ESPHome component when WLED already exists? 

That’s a great question! To answer it, let me take you back a few years.

### My Tasmota Roots

Before I started this project, I had the privilege of serving as a **Core Developer for Tasmota**. 

That experience was truly my "masterclass" in IoT. Working deep in the Tasmota code taught me the importance of stability, efficiency, and how to squeeze every drop of performance out of a microcontroller. It shaped how I look at smart home reliability.

So, when I renovated my house, I put those lessons to work. I ran Ethernet cables to every single room—because as good as WiFi mesh is, nothing beats the stability of a physical wire.

### The DIY Phase & The Hard Way

Initially, I went the "hard mode" route. I developed my own custom firmware from scratch, running on WT32-ETH01 modules (hardwired via Ethernet) to control relays, switches, and I2C sensors.

It worked, and it was super minimal. But technology moves fast! I soon realized that maintaining a 100% custom firmware every time I wanted to add a new gadget was becoming a full-time job. I needed something easier to manage.

I decided to give ESPHome a second chance. Years ago, I found it a bit clunky, but I was curious to see how it had matured.

### The moment "Dad, can I have rainbow lights?"

Then came the real challenge: my youngest daughter asked for RGB lights in her room.

Now, we all know **WLED** is the king of RGB control. It’s simply fantastic. But I had a dilemma:
1.  **Redundancy:** I already had a powerful ESP32 node in her room for the sensors and relays. Adding a *second* chip just for lights felt like a waste of resources.
2.  **Connectivity:** I didn't have spare Ethernet ports, so I’d have to force the WLED chip onto WiFi, breaking my "wired-only" rule.

I looked at my existing Ethernet nodes and thought, *"I have the power right here. Why duplicate hardware?"*

### The "Spaghetti Code" Problem

I turned to ESPHome to handle the LEDs. While powerful, let's be honest: its native effect library is pretty basic compared to WLED.

I tried using `addressable_lambda` to recreate WLED's magic, but it got messy fast. Recreating just *one* of my daughter’s favorite effects ("Aurora") took **341 lines of lambda code!** 

Imagine adding three or four effects—you’re looking at a config file with over 1,000 lines just for pretty lights. As a developer who values clean code, I couldn't live with that maintenance nightmare.

### Enter: ChimeraFX

I wanted the structure and integration of ESPHome, but with the amazing visual library of WLED.

So, I built **ChimeraFX**.

It allows you to keep your robust, single-chip setup while enjoying those fancy lighting effects without the messy code. It’s the best of both worlds, born from a lot of trial, error, and dad-duties.

I hope you enjoy using it as much as I enjoyed building it!
