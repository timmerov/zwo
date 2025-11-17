/*
Copyright (C) 2012-2024 tim cotter. All rights reserved.
*/

/**
drive the zwo asi astrophotography camera and the ioptron smarteq pro(+) mount.

launch the threads that do the actual work.
create the containers for them to exchange data.
**/

#include <aggiornamento/aggiornamento.h>
#include <aggiornamento/log.h>
#include <aggiornamento/thread.h>

#include <aggiornamento/master.h>
#include <aggiornamento/nonblocking_input.h>

#include <shared/image_double_buffer.h>
#include <shared/settings_buffer.h>


/** threads defined elsewhere. **/
extern agm::Thread *createCaptureThread(ImageDoubleBuffer *image_double_buffer, SettingsBuffer *settings_buffer);
extern agm::Thread *createWindowThread(ImageDoubleBuffer *image_double_buffer, SettingsBuffer *settings_buffer);
extern agm::Thread *createMenuThread(SettingsBuffer *settings_buffer);


/** start logging and all threads. **/
int main(
    int argc, char *argv[]
) noexcept {
    (void) argc;
    (void) argv;

    agm::log::init(TARGET_NAME ".log");

    /** create the containers. **/
    auto image_double_buffer = ImageDoubleBuffer::create();
    SettingsBuffer settings_buffer;

    /** store the containers. **/
    std::vector<agm::Container *> containers;
    containers.push_back(image_double_buffer);

    /** create the threads. **/
    std::vector<agm::Thread *> threads;
    threads.push_back(createCaptureThread(image_double_buffer, &settings_buffer));
    threads.push_back(createWindowThread(image_double_buffer, &settings_buffer));
    threads.push_back(createMenuThread(&settings_buffer));

    /** run the threads one of them stops all of them. **/
    agm::Thread::runAll(threads, containers);

    /**
    everything at the this point is dead.
    except for non-blocking input.
    cause it blocks.
    **/
    std::quick_exit(0);
    //return 0;
}
