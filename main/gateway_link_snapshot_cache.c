#include "gateway_link_snapshot_cache.h"

#include <string.h>

static bool same_input(const gateway_input_id_t *a, const gateway_input_id_t *b)
{
    return a != NULL && b != NULL &&
        a->source == b->source &&
        a->channel == b->channel &&
        strcmp(a->id, b->id) == 0;
}

void gateway_link_snapshot_cache_init(gateway_link_snapshot_cache_t *cache)
{
    if (cache != NULL) {
        memset(cache, 0, sizeof(*cache));
    }
}

bool gateway_link_snapshot_cache_update(
    gateway_link_snapshot_cache_t *cache,
    const gateway_link_input_descriptor_t *descriptor)
{
    if (cache == NULL || descriptor == NULL || descriptor->input.id[0] == '\0') {
        return false;
    }

    gateway_link_snapshot_entry_t *target = NULL;
    gateway_link_snapshot_entry_t *free_entry = NULL;
    for (size_t i = 0U; i < GATEWAY_LINK_SNAPSHOT_CACHE_CAPACITY; ++i) {
        gateway_link_snapshot_entry_t *entry = &cache->entries[i];
        if (entry->in_use && same_input(&entry->descriptor.input, &descriptor->input)) {
            target = entry;
            break;
        }
        if (!entry->in_use && free_entry == NULL) {
            free_entry = entry;
        }
    }
    if (target == NULL) {
        target = free_entry;
    }
    if (target == NULL) {
        return false;
    }

    if (!target->in_use) {
        memset(target, 0, sizeof(*target));
        target->in_use = true;
        target->descriptor.input = descriptor->input;
    }
    target->descriptor.available = descriptor->available;
    target->descriptor.profile = descriptor->profile;
    if (descriptor->manufacturer[0] != '\0') {
        strncpy(target->descriptor.manufacturer,
                descriptor->manufacturer,
                sizeof(target->descriptor.manufacturer) - 1U);
        target->descriptor.manufacturer[sizeof(target->descriptor.manufacturer) - 1U] = '\0';
    }
    if (descriptor->model[0] != '\0') {
        strncpy(target->descriptor.model,
                descriptor->model,
                sizeof(target->descriptor.model) - 1U);
        target->descriptor.model[sizeof(target->descriptor.model) - 1U] = '\0';
    }
    return true;
}

bool gateway_link_snapshot_cache_copy_slot(
    const gateway_link_snapshot_cache_t *cache,
    size_t slot,
    gateway_link_input_descriptor_t *descriptor)
{
    if (cache == NULL || descriptor == NULL ||
        slot >= GATEWAY_LINK_SNAPSHOT_CACHE_CAPACITY ||
        !cache->entries[slot].in_use) {
        return false;
    }
    *descriptor = cache->entries[slot].descriptor;
    return true;
}

size_t gateway_link_snapshot_cache_count(const gateway_link_snapshot_cache_t *cache)
{
    if (cache == NULL) {
        return 0U;
    }
    size_t count = 0U;
    for (size_t i = 0U; i < GATEWAY_LINK_SNAPSHOT_CACHE_CAPACITY; ++i) {
        if (cache->entries[i].in_use) {
            ++count;
        }
    }
    return count;
}
