/*
 * This file is part of clr-boot-manager.
 *
 * Copyright © 2016 Intel Corporation
 *
 * clr-boot-manager is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <getopt.h>
#include <string.h>
#include <unistd.h>

#include "bootman.h"
#include "cli.h"

bool cbm_command_update(int argc, char **argv)
{
        autofree(char) *root = NULL;
        autofree(BootManager) *manager = NULL;
        bool image_mode = false;

        if (!cli_default_args_init(&argc, &argv, &root)) {
                return false;
        }

        manager = boot_manager_new();
        if (!manager) {
                DECLARE_OOM();
                return false;
        }

        /* Allow mounting of the ESP */
        boot_manager_set_can_mount(manager, true);

        if (root) {
                autofree(char) *realp = NULL;

                realp = realpath(root, NULL);
                if (!realp) {
                        LOG("Path specified does not exist: %s\n", root);
                }
                /* Anything not / is image mode */
                if (!streq(realp, "/")) {
                        image_mode = true;
                }

                /* CBM will check this again, we just needed to check for
                 * image mode.. */
                if (!boot_manager_set_prefix(manager, root)) {
                        return false;
                }
        }

        /* Set the image mode */
        boot_manager_set_image_mode(manager, image_mode);

        /* Let CBM take care of the rest */
        return boot_manager_update(manager);
}

/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * vi: set shiftwidth=8 tabstop=8 expandtab:
 * :indentSize=8:tabSize=8:noTabs=true:
 */
