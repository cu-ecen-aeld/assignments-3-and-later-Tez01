/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer implementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"


/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    /**
    * TODO: implement per description
    */
   int curr_offset = char_offset;
   int curr_elem = buffer->out_offs;
   
   // Handle edge case when buffer might be full
    if((curr_elem == buffer->in_offs) && 
        (buffer->full == true)){
        // curr == in but buffer is full
        goto CHKFR_OFFSET;
    }

    while(curr_elem != buffer->in_offs){
        // buffer is not full and curr != in or
        // curr == in but buffer is full

CHKFR_OFFSET:        
	;
        size_t curr_size = buffer->entry[curr_elem].size;
        if(curr_offset <= (curr_size - 1)){ // NOTE: Assumed curr_elem->num_elements atleast 1 
            // offset lies in current element
            *entry_offset_byte_rtn = curr_offset;
            return &(buffer->entry[curr_elem]);
        }

        
        // offset futher away in buffer
        curr_offset = curr_offset - curr_size;

        curr_elem++;
        if(curr_elem >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED){
            // roll over
            curr_elem = 0;
        }

    }

    return NULL;
}


/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer,
     const struct aesd_buffer_entry *add_entry)
{
    /**
    * TODO: implement per description
    */
    
    if(buffer->full == true){ 
        // remove oldest entry
        
        buffer->cur_size -= (buffer->entry)[buffer->out_offs].size;
        buffer->out_offs++;
        if(buffer->out_offs >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED){
            // roll over
            buffer->out_offs = 0;
        }
    }

    // add new entry
    buffer->entry[buffer->in_offs] = *add_entry;
    buffer->cur_size += add_entry->size;
    buffer->in_offs++;
    if(buffer->in_offs >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED){
        // roll over
        buffer->in_offs = 0;
    }

    if(buffer->in_offs == buffer->out_offs){
        buffer->full = true;
    }

}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}


// Reasons to pass pointer into function rather than element itself
// 1. Use same code for multiple data
//         Object oriented paradigm -pointer is object and function is method
// 2. Sometimes we want to change actual element in place, rather than copy
// 3. efficient if arguments are large, 
//         eg: passing large struct is inefficient
