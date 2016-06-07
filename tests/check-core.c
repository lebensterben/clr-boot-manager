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
#include <check.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "bootman.h"
#include "config.h"
#include "files.h"
#include "nica/array.h"
#include "nica/files.h"
#include "util.h"
#include "util.h"

/**
 * Ensure scope based management is functional
 */
static bool reclaimed = false;
typedef char memtestchar;

struct TestKernel {
        const char *test_str;
        SystemKernel expected;
};

static void reaper(void *v)
{
        free(v);
        v = NULL;
        fprintf(stderr, "Freeing tmp var\n");
        reclaimed = true;
}
DEF_AUTOFREE(memtestchar, reaper)

START_TEST(bootman_memory_test)
{
        {
                autofree(memtestchar) *tmp = NULL;

                if (!asprintf(&tmp, "Allocation test")) {
                        fail("Unable to allocate memory");
                }
        }
        fail_if(!reclaimed, "Scope based tmp var was not reclaimed!");
}
END_TEST

START_TEST(bootman_new_test)
{
        autofree(BootManager) *m = NULL;

        m = boot_manager_new();
        fail_if(!m, "Failed to construct BootManager instance");
}
END_TEST

START_TEST(bootman_parser_test)
{
        /* We know these will fail */
        const char *ridiculous[] = {
                "0",    NULL,     "4.30", ".-",    ".",      "@",
                "@!_+", "4.4.0-", ".0-",  ".-lts", "0.-lts", "4.0.20-190.",
        };

        const struct TestKernel valid[] = {
                { "4.4.0-120.lts", { "4.4.0", "lts", 120 } },
                { "4-120.l", { "4", "l", 120 } },
                { "1.2.3.4.5-6.native", { "1.2.3.4.5", "native", 6 } },
                { "4.4.4-120.kvm", { "4.4.4", "kvm", 120 } },
        };

        SystemKernel k = { 0 };

        for (size_t i = 0; i < ARRAY_SIZE(ridiculous); i++) {
                const char *sz = ridiculous[i];
                fail_if(cbm_parse_system_kernel(sz, &k), "Parsed broken format");
        }

        for (size_t i = 0; i < ARRAY_SIZE(valid); i++) {
                const struct TestKernel exp = valid[i];
                bool parsed = cbm_parse_system_kernel(exp.test_str, &k);
                fail_if(!parsed, "Failed to parse valid kernel name");
                fail_if(!streq(exp.expected.ktype, k.ktype), "Failed to match kernel type");
                fail_if(!streq(exp.expected.version, k.version), "Failed to match kernel version");
                fail_if(exp.expected.release != k.release, "Failed to match kernel release");
        }
}
END_TEST

int kernel_compare(const void *a, const void *b)
{
        const Kernel *ka = *(const Kernel **)a;
        const Kernel *kb = *(const Kernel **)b;

        if (ka->release < kb->release) {
                return -1;
        }
        return 1;
}

int kernel_compare_reverse(const void *a, const void *b)
{
        const Kernel *ka = *(const Kernel **)a;
        const Kernel *kb = *(const Kernel **)b;

        if (ka->release > kb->release) {
                return -1;
        }
        return 1;
}

START_TEST(bootman_list_kernels_test)
{
        autofree(BootManager) *m = NULL;
        autofree(KernelArray) *list = NULL;
        const Kernel *kernel = NULL;

        m = boot_manager_new();
        fail_if(boot_manager_set_prefix(m, "/ro347u59jaowlq'#1'1'1'1aaaaa,*"),
                "set_prefix should fail for non existent directory");

        boot_manager_set_prefix(m, (char *)TOP_BUILD_DIR "/tests/dummy_install");

        list = boot_manager_get_kernels(m);
        fail_if(!list, "Failed to list kernels");

        fail_if(list->len != 4, "Invalid number of discovered kernels");

        /* Normal sort test */
        nc_array_qsort(list, kernel_compare);
        kernel = nc_array_get(list, 0);
        fail_if(kernel->release != 121, "Invalid first element");
        kernel = nc_array_get(list, 1);
        fail_if(kernel->release != 124, "Invalid second element");
        kernel = nc_array_get(list, 2);
        fail_if(kernel->release != 137, "Invalid third element");
        kernel = nc_array_get(list, 3);
        fail_if(kernel->release != 138, "Invalid fourth element");

        /* Reverse sort test */
        nc_array_qsort(list, kernel_compare_reverse);
        kernel = nc_array_get(list, 0);
        fail_if(kernel->release != 138, "Invalid first reversed element");
        kernel = nc_array_get(list, 1);
        fail_if(kernel->release != 137, "Invalid second reversed element");
        kernel = nc_array_get(list, 2);
        fail_if(kernel->release != 124, "Invalid third reversed element");
        kernel = nc_array_get(list, 3);
        fail_if(kernel->release != 121, "Invalid fourth reversed element");
}
END_TEST

START_TEST(bootman_map_kernels_test)
{
        autofree(BootManager) *m = NULL;
        autofree(KernelArray) *list = NULL;
        autofree(NcHashmap) *map = NULL;
        KernelArray *get = NULL;
        Kernel *default_kernel = NULL;

        m = boot_manager_new();
        boot_manager_set_prefix(m, (char *)TOP_BUILD_DIR "/tests/dummy_install");

        list = boot_manager_get_kernels(m);
        fail_if(!list, "Failed to list kernels");
        map = boot_manager_map_kernels(m, list);
        fail_if(!map, "Failed to map kernels");

        fail_if(nc_hashmap_size(map) != 2, "Invalid size for mapping test");

        /* KVM type test */
        get = nc_hashmap_get(map, "kvm");
        fail_if(!get, "Failed to get KVM type list");
        fail_if(get->len != 2, "Incorrect list length for kvm");
        get = NULL;

        /* Native type test */
        get = nc_hashmap_get(map, "native");
        fail_if(!get, "Failed to get native type list");
        fail_if(get->len != 2, "Incorrect list length for native");
        get = NULL;

        /* default-kvm = "org.clearlinux.kvm.4.2.3-124" */
        default_kernel = boot_manager_get_default_for_type(m, list, "kvm");
        fail_if(!default_kernel, "Failed to find default kvm kernel");
        fail_if(default_kernel->release != 124, "Mismatched kvm default release");
        fail_if(!streq(default_kernel->version, "4.2.3"), "Mismatched kvm default version");
        fail_if(!streq(default_kernel->ktype, "kvm"), "Mismatched kvm default type");
        default_kernel = NULL;

        /* default-native = "org.clearlinux.native.4.2.3-138" */
        default_kernel = boot_manager_get_default_for_type(m, list, "native");
        fail_if(!default_kernel, "Failed to find default native kernel");
        fail_if(default_kernel->release != 138, "Mismatched native default release");
        fail_if(!streq(default_kernel->version, "4.2.3"), "Mismatched native default version");
        fail_if(!streq(default_kernel->ktype, "native"), "Mismatched native default type");
}
END_TEST

START_TEST(bootman_install_kernel_test)
{
        autofree(BootManager) *m = NULL;
        autofree(KernelArray) *list = NULL;
        __attribute__((unused)) const Kernel *kernel = NULL;

        m = boot_manager_new();
        boot_manager_set_prefix(m, (char *)TOP_BUILD_DIR "/tests/dummy_install");
        list = boot_manager_get_kernels(m);

        nc_array_qsort(list, kernel_compare_reverse);
        /* org.clearlinux.native.4.2.3-138 */
        kernel = nc_array_get(list, 0);

        fail_if(!boot_manager_install_kernel(m, kernel), "Failed to install kernel");

        fail_if(!nc_file_exists(TOP_BUILD_DIR "/tests/dummy_install/" BOOT_DIRECTORY
                                              "/loader/entries/Clear-linux-native-4.2.3-138.conf"),
                "Failed to find loader .conf entry");

        fail_if(!nc_file_exists(TOP_BUILD_DIR "/tests/dummy_install/" BOOT_DIRECTORY
                                              "/org.clearlinux.native.4.2.3-138"),
                "Failed to find kernel file after install");
}
END_TEST

START_TEST(bootman_set_default_kernel_test)
{
        autofree(BootManager) *m = NULL;
        autofree(KernelArray) *list = NULL;
        __attribute__((unused)) const Kernel *kernel = NULL;
        const char *expected = "default Clear-linux-native-4.2.3-138\n";
        autofree(char) *got = NULL;

        m = boot_manager_new();
        boot_manager_set_prefix(m, (char *)TOP_BUILD_DIR "/tests/dummy_install");
        list = boot_manager_get_kernels(m);

        nc_array_qsort(list, kernel_compare_reverse);
        /* org.clearlinux.native.4.2.3-138 */
        kernel = nc_array_get(list, 0);

        fail_if(!boot_manager_set_default_kernel(m, kernel), "Failed to set default kernel");

        fail_if(!file_get_text(TOP_BUILD_DIR "/tests/dummy_install/" BOOT_DIRECTORY
                                             "/loader/loader.conf",
                               &got),
                "Failed to read loader.conf");

        fail_if(!streq(expected, got), "loader.conf does not match expected data");
}
END_TEST

/* Take advantage of previously installed kernel */
START_TEST(bootman_remove_kernel_test)
{
        autofree(BootManager) *m = NULL;
        autofree(KernelArray) *list = NULL;
        __attribute__((unused)) const Kernel *kernel = NULL;

        m = boot_manager_new();
        boot_manager_set_prefix(m, (char *)TOP_BUILD_DIR "/tests/dummy_install");
        list = boot_manager_get_kernels(m);

        nc_array_qsort(list, kernel_compare_reverse);
        /* org.clearlinux.native.4.2.3-138 */
        kernel = nc_array_get(list, 0);

        fail_if(!boot_manager_remove_kernel(m, kernel), "Failed to remove kernel");

        fail_if(nc_file_exists(TOP_BUILD_DIR "/tests/dummy_install/" BOOT_DIRECTORY
                                             "/loader/entries/Clear-linux-native-4.2.3-138.conf"),
                "Failed to remove loader .conf entry");

        fail_if(nc_file_exists(TOP_BUILD_DIR "/tests/dummy_install/" BOOT_DIRECTORY
                                             "/org.clearlinux.native.4.2.3-138"),
                "Failed to remove kernel file after install");
}
END_TEST

START_TEST(bootman_purge_test)
{
        autofree(BootManager) *m = NULL;
        autofree(KernelArray) *list = NULL;
        __attribute__((unused)) const Kernel *kernel = NULL;

        m = boot_manager_new();
        boot_manager_set_prefix(m, (char *)TOP_BUILD_DIR "/tests/dummy_install");
        list = boot_manager_get_kernels(m);

        for (int i = 0; i < list->len; i++) {
                kernel = nc_array_get(list, i);
                fail_if(!boot_manager_install_kernel(m, kernel), "Failed to install known kernel");
        }

        /* Uncomment for validation
        __attribute__((unused)) int r = system("tree " TOP_BUILD_DIR "/tests/dummy_install"); */

        for (int i = 0; i < list->len; i++) {
                kernel = nc_array_get(list, i);
                fail_if(!boot_manager_remove_kernel(m, kernel), "Failed to remove known kernel");
        }

        /* Second part of validation
        r = system("tree " TOP_BUILD_DIR "/tests/dummy_install");*/

        nc_array_free(&list, (array_free_func)free_kernel);

        list = boot_manager_get_kernels(m);

        fail_if(list->len != 0, "Unaccounted kernels left in test tree");
}
END_TEST

START_TEST(bootman_install_bootloader_test)
{
        autofree(BootManager) *m = NULL;

        m = boot_manager_new();
        boot_manager_set_prefix(m, (char *)TOP_BUILD_DIR "/tests/dummy_install");

        fail_if(!boot_manager_modify_bootloader(m,
                                                BOOTLOADER_OPERATION_INSTALL |
                                                    BOOTLOADER_OPERATION_NO_CHECK),
                "Failed to install the bootloader");

        if (boot_manager_get_architecture_size(m) == 64) {
                fail_if(!nc_file_exists(TOP_BUILD_DIR "/tests/dummy_install/" BOOT_DIRECTORY
                                                      "/EFI/Boot/BOOTX64.EFI"),
                        "Main x64 bootloader missing");
#if defined(HAVE_SYSTEMD_BOOT)
                fail_if(!nc_file_exists(TOP_BUILD_DIR "/tests/dummy_install/" BOOT_DIRECTORY
                                                      "/EFI/systemd/systemd-bootx64.efi"),
                        "Systemd x64 bootloader missing");
#elif defined(HAVE_GUMMIBOOT)
                fail_if(!nc_file_exists(TOP_BUILD_DIR "/tests/dummy_install/" BOOT_DIRECTORY
                                                      "/EFI/gummiboot/gummibootx64.efi"),
                        "gummiboot x64 bootloader missing");
#else
                fail_if(!nc_file_exists(TOP_BUILD_DIR "/tests/dummy_install/" BOOT_DIRECTORY
                                                      "/EFI/goofiboot/goofibootx64.efi"),
                        "goofiboot x64 bootloader missing");
#endif
        } else {
                fail_if(!nc_file_exists(TOP_BUILD_DIR "/tests/dummy_install/" BOOT_DIRECTORY
                                                      "/EFI/Boot/BOOTIA32.EFI"),
                        "Main ia32 bootloader missing");
#if defined(HAVE_SYSTEMD_BOOT)
                fail_if(!nc_file_exists(TOP_BUILD_DIR "/tests/dummy_install/" BOOT_DIRECTORY
                                                      "/EFI/systemd/systemd-bootia32.efi"),
                        "systemd-boot ia32 bootloader missing");
#elif defined(HAVE_GUMMIBOOT)
                fail_if(!nc_file_exists(TOP_BUILD_DIR "/tests/dummy_install/" BOOT_DIRECTORY
                                                      "/EFI/gummiboot/gummibootia32.efi"),
                        "gummiboot ia32 bootloader missing");
#else
                fail_if(!nc_file_exists(TOP_BUILD_DIR "/tests/dummy_install/" BOOT_DIRECTORY
                                                      "/EFI/goofiboot/goofibootia32.efi"),
                        "goofiboot ia32 bootloader missing");
#endif
        }

        /* DEBUG:
        __attribute__ ((unused)) int r = system("tree " TOP_BUILD_DIR "/tests/dummy_install");*/
}
END_TEST

START_TEST(bootman_remove_bootloader_test)
{
        autofree(BootManager) *m = NULL;

        fail_if(!nc_file_exists(TOP_BUILD_DIR "/tests/dummy_install/" BOOT_DIRECTORY "/EFI/Boot"),
                "Main EFI directory missing, botched install");

        m = boot_manager_new();

        boot_manager_set_prefix(m, (char *)TOP_BUILD_DIR "/tests/dummy_install");

        fail_if(!boot_manager_modify_bootloader(m, BOOTLOADER_OPERATION_REMOVE),
                "Failed to remove the bootloader");

        /* Ensure that it is indeed removed. */
        if (boot_manager_get_architecture_size(m) == 64) {
                fail_if(nc_file_exists(TOP_BUILD_DIR "/tests/dummy_install" BOOT_DIRECTORY
                                                     "/EFI/Boot/BOOTX64.EFI"),
                        "Main x64 bootloader present");
#if defined(HAVE_SYSTEMD_BOOT)
                fail_if(nc_file_exists(TOP_BUILD_DIR "/tests/dummy_install/" BOOT_DIRECTORY
                                                     "/EFI/systemd"),
                        "Systemd x64 bootloader present");
#elif defined(HAVE_GUMMIBOOT)
                fail_if(nc_file_exists(TOP_BUILD_DIR "/tests/dummy_install/" BOOT_DIRECTORY
                                                     "/EFI/gummiboot"),
                        "gummiboot x64 bootloader present");
#else
                fail_if(nc_file_exists(TOP_BUILD_DIR "/tests/dummy_install/" BOOT_DIRECTORY
                                                     "/EFI/goofiboot"),
                        "goofiboot x64 bootloader present");
#endif
        } else {
                fail_if(nc_file_exists(TOP_BUILD_DIR "/tests/dummy_install/" BOOT_DIRECTORY
                                                     "/EFI/Boot/BOOTIA32.EFI"),
                        "Main ia32 bootloader present");
#if defined(HAVE_SYSTEMD_BOOT)
                fail_if(nc_file_exists(TOP_BUILD_DIR "/tests/dummy_install/" BOOT_DIRECTORY
                                                     "/EFI/systemd"),
                        "systemd-boot ia32 bootloader present");
#elif defined(HAVE_GUMMIBOOT)
                fail_if(nc_file_exists(TOP_BUILD_DIR "/tests/dummy_install/" BOOT_DIRECTORY
                                                     "/EFI/gummiboot"),
                        "gummiboot ia32 bootloader present");
#else
                fail_if(nc_file_exists(TOP_BUILD_DIR "/tests/dummy_install/" BOOT_DIRECTORY
                                                     "/EFI/goofiboot"),
                        "goofiboot ia32 bootloader present");
#endif
        }

        fail_if(nc_file_exists(TOP_BUILD_DIR "/tests/dummy_install/" BOOT_DIRECTORY
                                             "/loader/loader.conf"),
                "systemd-class loader.conf present");
        /* DEBUG:
        __attribute__ ((unused)) int r = system("tree " TOP_BUILD_DIR "/tests/dummy_install");*/
}
END_TEST

static Suite *core_suite(void)
{
        Suite *s = NULL;
        TCase *tc = NULL;

        s = suite_create("bootman_core");
        tc = tcase_create("bootman_core_functions");
        tcase_add_test(tc, bootman_new_test);
        tcase_add_test(tc, bootman_memory_test);
        tcase_add_test(tc, bootman_parser_test);
        suite_add_tcase(s, tc);

        tc = tcase_create("bootman_kernel_functions");
        tcase_add_test(tc, bootman_list_kernels_test);
        tcase_add_test(tc, bootman_map_kernels_test);
        tcase_add_test(tc, bootman_install_kernel_test);
        tcase_add_test(tc, bootman_set_default_kernel_test);
        tcase_add_test(tc, bootman_remove_kernel_test);
        tcase_add_test(tc, bootman_purge_test);
        tcase_add_test(tc, bootman_install_bootloader_test);
        tcase_add_test(tc, bootman_remove_bootloader_test);
        suite_add_tcase(s, tc);

        return s;
}

int main(void)
{
        Suite *s;
        SRunner *sr;
        int fail;

        /* syncing can be problematic during test suite runs */
        cbm_set_sync_filesystems(false);

        s = core_suite();
        sr = srunner_create(s);
        srunner_run_all(sr, CK_VERBOSE);
        fail = srunner_ntests_failed(sr);
        srunner_free(sr);

        if (fail > 0) {
                return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
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
