
# MLFQ Simulator

This project simulates a CPU scheduler using a 3 level Multi Lever Feed Back Queue within a UNIX based system. Multiple cores can be simulated and the priorities can be reset every given number of miliseconds. Each core and queue is given its own thread so they operate independently, then a summary oh the average response and turnaround times are reported. Users can use this to try out many Core count and reset configurations to see what gives the better times.


## Authors

- [@AlexanderLannon](https://github.com/CyborgWhiskey)


## Compiling

Can be compiled by typing make though a already compiled file is provided


## Excecution

To run the user must type './MLFQ "#Cores" "S" "tasks.txt" where:

- #Cores is the number of cores to simulate
- S is the time (in miliseconds) between priority resets
- tasks.txt is a file containing a list of instructions to simulate on the cores

The task file contains commands seperated by new lines. These commands are formatted as "name id length IOChance" where:

- name: Name of the task
- id: numeric id of the task
- length: Time (in micro seconds) until task completion
- IOChance: Chance for an IO event to occur each time the task is run on a cores

The task file can also contain delays in instruction delivery so that not all instructions are added to the MLFQ at one time. This is done with "DELAY time" where time is the length of the delay in miliseconds

There is an example 'tasks.txt' file provided for reference