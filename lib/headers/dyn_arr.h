#pragma once


#include <stddef.h>

// An array which will grow its size when it becomes full. Access the values with
// the `data` field. Always retrieve the pointer in `data` anew after a call to
// `DynamicArray_push` is made. Access the number of elements with `num_elements`.
struct DynamicArray {
	unsigned int num_elements;
	unsigned int capacity;
	size_t item_size;
	void* data;
};

struct DynamicArray DynamicArray_new(size_t item_size, unsigned int capacity);
void DynamicArray_push(struct DynamicArray* array, void* item);
void DynamicArray_remove(struct DynamicArray* array, size_t index);
void DynamicArray_removeOrdered(struct DynamicArray* array, size_t index);
void DynamicArray_clear(struct DynamicArray* array);
void DynamicArray_free(struct DynamicArray* array);
