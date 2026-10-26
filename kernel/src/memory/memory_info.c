#include "memory_info.h"

#include <stdint.h>
#include <limine.h>

#include "console/console.h"
#include "boot/limine_requests.h"

static const char *memory_type_name(uint64_t type) {
    switch (type) {
        case LIMINE_MEMMAP_USABLE:                 return "nutzbar";
        case LIMINE_MEMMAP_RESERVED:               return "reserviert";
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE:       return "ACPI rueckforderbar";
        case LIMINE_MEMMAP_ACPI_NVS:               return "ACPI NVS";
        case LIMINE_MEMMAP_BAD_MEMORY:             return "defekter Speicher";
        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE: return "Bootloader rueckforderbar";
        case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES: return "Kernel/Module";
        case LIMINE_MEMMAP_FRAMEBUFFER:            return "Framebuffer";
        case LIMINE_MEMMAP_RESERVED_MAPPED:        return "reserviert (gemappt)";
        default:                                    return "unbekannt";
    }
}

void memory_info_print(void) {
    console_write("\n=== Limine Memory Information ===\n");

    if (limine_hhdm_request.response != NULL) {
        console_write("HHDM offset: ");
        console_write_hex(limine_hhdm_request.response->offset);
        console_write("\n");
    } else {
        console_write("HHDM offset: not provided\n");
    }

    if (limine_memmap_request.response == NULL ||
        limine_memmap_request.response->entries == NULL) {
        console_write("Limine memory map is not available.\n");
        return;
    }

    struct limine_memmap_response *map = limine_memmap_request.response;
    uint64_t usable_bytes = 0;

    console_write("Memory map entries: ");
    console_write_u64(map->entry_count);
    console_write("\n");

    for (uint64_t i = 0; i < map->entry_count; ++i) {
        const struct limine_memmap_entry *entry = map->entries[i];
        if (entry == NULL) {
            continue;
        }

        if (entry->type == LIMINE_MEMMAP_USABLE) {
            usable_bytes += entry->length;
        }

        console_write("  [");
        console_write(memory_type_name(entry->type));
        console_write("] base=");
        console_write_hex(entry->base);
        console_write(" length=");
        console_write_u64(entry->length);
        console_write(" bytes\n");
    }

    console_write("\nRAM reported as usable by Limine: ");
    console_write_u64(usable_bytes);
    console_write(" bytes (about ");
    console_write_u64(usable_bytes / (1024 * 1024));
    console_write(" MiB)\n");
    console_write("Hinweis: Das ist nutzbarer Speicher laut Bootloader, noch kein Freispeicher-Zaehler des OS-Allokators.\n");
}
