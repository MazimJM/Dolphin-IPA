#include <stdlib.h>
#include <string.h>
#include <dispatch/dispatch.h>
#include <os/lock.h>

// GameCube Memory Map
// 24MB Main RAM (0x00000000 - 0x01800000)
// 16MB Audio RAM (0x04000000 - 0x05000000)

class GameCubeMemory {
private:
    static const size_t MAIN_RAM_SIZE = 24 * 1024 * 1024;    // 24MB
    static const size_t AUDIO_RAM_SIZE = 16 * 1024 * 1024;   // 16MB
    static const size_t CACHE_LINE = 64;
    
    uint8_t* main_ram;
    uint8_t* audio_ram;
    
    dispatch_queue_t mem_queue;
    os_unfair_lock mem_lock;
    
    // Memory Access Cache for iPhone 11
    struct CacheEntry {
        uint32_t address;
        uint32_t data;
        uint8_t valid;
    } cache[256];
    
public:
    GameCubeMemory() {
        main_ram = (uint8_t*)aligned_alloc(CACHE_LINE, MAIN_RAM_SIZE);
        audio_ram = (uint8_t*)aligned_alloc(CACHE_LINE, AUDIO_RAM_SIZE);
        
        mem_queue = dispatch_queue_create("com.dolphin.memory", DISPATCH_QUEUE_SERIAL);
        os_unfair_lock_init(&mem_lock);
        
        memset(main_ram, 0, MAIN_RAM_SIZE);
        memset(audio_ram, 0, AUDIO_RAM_SIZE);
        memset(cache, 0, sizeof(cache));
    }
    
    ~GameCubeMemory() {
        free(main_ram);
        free(audio_ram);
    }
    
    // Fast memory read with cache
    uint32_t read32(uint32_t address) {
        os_unfair_lock_lock(&mem_lock);
        
        // Check cache first
        uint32_t cache_idx = (address >> 6) & 0xFF;
        if(cache[cache_idx].valid && cache[cache_idx].address == address) {
            uint32_t data = cache[cache_idx].data;
            os_unfair_lock_unlock(&mem_lock);
            return data;
        }
        
        uint32_t data = 0;
        
        // Main RAM access
        if(address < MAIN_RAM_SIZE) {
            memcpy(&data, &main_ram[address], sizeof(uint32_t));
        }
        // Audio RAM access
        else if(address >= 0x04000000 && address < 0x04000000 + AUDIO_RAM_SIZE) {
            memcpy(&data, &audio_ram[address - 0x04000000], sizeof(uint32_t));
        }
        
        // Update cache
        cache[cache_idx].address = address;
        cache[cache_idx].data = data;
        cache[cache_idx].valid = 1;
        
        os_unfair_lock_unlock(&mem_lock);
        return data;
    }
    
    // Fast memory write
    void write32(uint32_t address, uint32_t data) {
        os_unfair_lock_lock(&mem_lock);
        
        if(address < MAIN_RAM_SIZE) {
            memcpy(&main_ram[address], &data, sizeof(uint32_t));
        }
        else if(address >= 0x04000000 && address < 0x04000000 + AUDIO_RAM_SIZE) {
            memcpy(&audio_ram[address - 0x04000000], &data, sizeof(uint32_t));
        }
        
        // Invalidate cache for this address
        uint32_t cache_idx = (address >> 6) & 0xFF;
        cache[cache_idx].valid = 0;
        
        os_unfair_lock_unlock(&mem_lock);
    }
    
    // Bulk memory copy (for DMA operations)
    void memcpy_fast(uint32_t dest, uint32_t src, size_t size) {
        os_unfair_lock_lock(&mem_lock);
        
        if(dest < MAIN_RAM_SIZE && src < MAIN_RAM_SIZE) {
            memmove(&main_ram[dest], &main_ram[src], size);
        }
        
        os_unfair_lock_unlock(&mem_lock);
    }
    
    // Get raw pointer for direct access (use with caution)
    uint8_t* get_main_ram() { return main_ram; }
    uint8_t* get_audio_ram() { return audio_ram; }
};