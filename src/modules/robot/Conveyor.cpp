/*
      This file is part of Smoothie (http://smoothieware.org/). The motion control part is heavily based on Grbl (https://github.com/simen/grbl) with additions from Sungeun K. Jeon (https://github.com/chamnit/grbl)
      Smoothie is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
      Smoothie is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
      You should have received a copy of the GNU General Public License along with Smoothie. If not, see <http://www.gnu.org/licenses/>.
*/

using namespace std;
#include <vector>
#include "libs/nuts_bolts.h"
#include "libs/RingBuffer.h"
#include "../communication/utils/Gcode.h"
#include "libs/Module.h"
#include "libs/Kernel.h"
#include "Timer.h" // mbed.h lib
#include "wait_api.h" // mbed.h lib
#include "Block.h"
#include "Conveyor.h"
#include "Planner.h"

// The conveyor holds the queue of blocks, takes care of creating them, and starting the executing chain of blocks

Conveyor::Conveyor()
{
    current_action_index = queue.tail;
    current_action = NULL;
    queue.get_head_ref()->conveyor = this;
}

void Conveyor::on_module_loaded(){
    register_for_event(ON_IDLE);
    register_for_event(ON_SECOND_TICK);
}

// Delete blocks here, because they can't be deleted in interrupt context ( see Block.cpp:release )
void Conveyor::on_idle(void* argument)
{
    // clean up completed actions
    if (current_action)
    {
        while (queue.get_tail_ref() != current_action)
            queue.consume_tail()->clean();
    }
    else if (queue.empty() == false)
        queue.consume_tail()->clean();

    if (current_action == NULL && queue.get_head_ref()->first_data)
    {
        commit_action();
        current_action->invoke();
    }
}

// for debugging
void Conveyor::on_second_tick(void* argument)
{
//     printf("Conveyor::current_action: %p\n", current_action);
//     printf("Queue: head:%d tail:%d\n", queue.head, queue.tail);
//     printf("Tail Data: %p\n", queue.get_tail_ref()->first_data);
}

Action* Conveyor::next_action(void)
{
    return queue.get_head_ref();
}

void Conveyor::start_next_action()
{
    printf("Conveyor::start_next_action(%p)\n", current_action);

    if (queue.empty())
    {
        current_action = NULL;
        return;
    }

    current_action_index = queue.next_block_index(current_action_index);
    current_action = queue.get_ref(current_action_index);

    if (current_action->first_data)
        current_action->invoke();
    else
        current_action = NULL;
}

// call this when _next_action is ready to be executed
Action* Conveyor::commit_action()
{
    printf("Conveyor::commit_action(%p)\n", queue.get_head_ref());

    Action* added_action = queue.get_head_ref();

    while (queue.full());
    queue.produce_head();

    queue.get_head_ref()->conveyor = this;

    // if queue was empty, restart it
    __disable_irq();
    if (current_action == NULL)
        current_action = queue.get_tail_ref();
    __enable_irq();

    return added_action;
}

// Wait for the queue to have a given number of free blocks
void Conveyor::wait_for_queue(int free_blocks)
{
    while( this->queue.size() >= this->queue.capacity() - free_blocks )
    {
        this->kernel->call_event(ON_IDLE);
    }
}

// Wait for the queue to be empty
void Conveyor::wait_for_empty_queue()
{
    while( this->queue.size() > 0)
    {
        this->kernel->call_event(ON_IDLE);
    }
}