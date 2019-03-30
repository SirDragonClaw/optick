#pragma once

#include "Common.h"

#include <cstring>
#include <new>
#include <stdlib.h>
#include <xmmintrin.h>

namespace Optick
{
	class Memory
	{
	public:
		static void* Alloc(size_t size, size_t align = 16)
		{
			return _mm_malloc(size, align);
		}

		static void Free(void* p)
		{
			_mm_free(p);
		}

		template<class T>
		static T* New()
		{
			return new (Memory::Alloc(sizeof(T))) T();
		}

		template<class T, class P1>
		static T* New(P1 p1)
		{
			return new (Memory::Alloc(sizeof(T))) T(p1);
		}

		template<class T, class P1, class P2>
		static T* New(P1 p1, P2 p2)
		{
			return new (Memory::Alloc(sizeof(T))) T(p1, p2);
		}

		template<class T>
		static void Delete(T* p)
		{
			if (p)
			{
				p->~T();
				Free(p);
			}
		}
	};


	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	template<class T, uint32 SIZE>
	struct MemoryChunk
	{
		OPTICK_ALIGN_CACHE T data[SIZE];
		MemoryChunk* next;
		MemoryChunk* prev;

		MemoryChunk() : next(0), prev(0) {}

		~MemoryChunk()
		{
			MemoryChunk* chunk = this;
			while (chunk->next)
				chunk = chunk->next;

			while (chunk != this)
			{
				MemoryChunk* toDelete = chunk;
				chunk = toDelete->prev;
				Memory::Delete(toDelete);
			}

			if (prev != nullptr)
			{
				prev->next = nullptr;
				prev = nullptr;
			}
		}
	};
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	template<class T, uint32 SIZE = 16>
	class MemoryPool
	{
		typedef MemoryChunk<T, SIZE> Chunk;
		Chunk* root;
		Chunk* chunk;
		uint32 index;

		OPTICK_INLINE void AddChunk()
		{
			index = 0;
			if (!chunk || !chunk->next)
			{
				Chunk* newChunk = Memory::New<Chunk>();
				if (chunk)
				{
					chunk->next = newChunk;
					newChunk->prev = chunk;
					chunk = newChunk;
				}
				else
				{
					root = chunk = newChunk;
				}
			}
			else
			{
				chunk = chunk->next;
			}
		}
	public:
		MemoryPool() : root(nullptr), chunk(nullptr), index(SIZE) {}

		OPTICK_INLINE T& Add()
		{
			if (index >= SIZE)
				AddChunk();

			return chunk->data[index++];
		}

		OPTICK_INLINE T& Add(const T& item)
		{
			return Add() = item;
		}

		OPTICK_INLINE T* AddRange(const T* items, size_t count, bool allowOverlap = true)
		{
			if (count == 0 || (count > SIZE && !allowOverlap))
				return nullptr;

			if (count >= (SIZE - index) && !allowOverlap)
			{
				AddChunk();
			}

			T* result = &chunk->data[index];

			while (count)
			{
				size_t numLeft = SIZE - index;
				size_t numCopy = numLeft < count ? numLeft : count;
				std::memcpy(&chunk->data[index], items, sizeof(T) * numCopy);

				count -= numCopy;
				items += numCopy;
				index += (uint32_t)numCopy;

				if (count)
					AddChunk();
			}

			return result;
		}


		OPTICK_INLINE T* TryAdd(int count)
		{
			if (index + count <= SIZE)
			{
				T* res = &chunk->data[index];
				index += count;
				return res;
			}

			return nullptr;
		}

		OPTICK_INLINE T* Back()
		{
			if (chunk && index > 0)
				return &chunk->data[index - 1];

			if (chunk && chunk->prev != nullptr)
				return &chunk->prev->data[SIZE - 1];

			return nullptr;
		}

		OPTICK_INLINE size_t Size() const
		{
			if (root == nullptr)
				return 0;

			size_t count = 0;

			for (const Chunk* it = root; it != chunk; it = it->next)
				count += SIZE;

			return count + index;
		}

		OPTICK_INLINE bool IsEmpty() const
		{
			return (chunk == nullptr) || (chunk == root && index == 0);
		}

		OPTICK_INLINE void Clear(bool preserveMemory = true)
		{
			if (!preserveMemory)
			{
				if (root)
				{
					Memory::Delete(root);
					root = nullptr;
					chunk = nullptr;
					index = SIZE;
				}
			}
			else if (root)
			{
				index = 0;
				chunk = root;
			}
		}

		class const_iterator
		{
			void advance()
			{
				if (chunkIndex < SIZE - 1)
				{
					++chunkIndex;
				}
				else
				{
					chunkPtr = chunkPtr->next;
					chunkIndex = 0;
				}
			}
		public:
			typedef const_iterator self_type;
			typedef T value_type;
			typedef T& reference;
			typedef T* pointer;
			typedef int difference_type;
			const_iterator(const Chunk* ptr, size_t index) : chunkPtr(ptr), chunkIndex(index) { }
			self_type operator++()
			{
				self_type i = *this;
				advance();
				return i;
			}
			self_type operator++(int junk)
			{
				advance();
				return *this;
			}
			reference operator*() { return (reference)chunkPtr->data[chunkIndex]; }
			const pointer operator->() { return &chunkPtr->data[chunkIndex]; }
			bool operator==(const self_type& rhs) { return (chunkPtr == rhs.chunkPtr) && (chunkIndex == rhs.chunkIndex); }
			bool operator!=(const self_type& rhs) { return (chunkPtr != rhs.chunkPtr) || (chunkIndex != rhs.chunkIndex); }
		private:
			const Chunk* chunkPtr;
			size_t chunkIndex;
		};

		const_iterator begin() const
		{
			return const_iterator(root, 0);
		}

		const_iterator end() const
		{
			return const_iterator(chunk, index);
		}

		template<class Func>
		void ForEach(Func func) const
		{
			for (const Chunk* it = root; it != chunk; it = it->next)
				for (uint32 i = 0; i < SIZE; ++i)
					func(it->data[i]);

			if (chunk)
				for (uint32 i = 0; i < index; ++i)
					func(chunk->data[i]);
		}

		template<class Func>
		void ForEach(Func func)
		{
			for (Chunk* it = root; it != chunk; it = it->next)
				for (uint32 i = 0; i < SIZE; ++i)
					func(it->data[i]);

			if (chunk)
				for (uint32 i = 0; i < index; ++i)
					func(chunk->data[i]);
		}

		template<class Func>
		void ForEachChunk(Func func) const
		{
			for (const Chunk* it = root; it != chunk; it = it->next)
				func(it->data, SIZE);

			if (chunk)
				func(chunk->data, index);
		}

		void ToArray(T* destination) const
		{
			uint32 curIndex = 0;

			for (const Chunk* it = root; it != chunk; it = it->next)
			{
				memcpy(&destination[curIndex], it->data, sizeof(T) * SIZE);
				curIndex += SIZE;
			}

			if (chunk && index > 0)
			{
				memcpy(&destination[curIndex], chunk->data, sizeof(T) * index);
			}
		}
	};
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	template<uint32 CHUNK_SIZE>
	class MemoryBuffer : private MemoryPool<uint8, CHUNK_SIZE>
	{
	public:
		template<class U>
		U* Add(U* data, size_t size, bool allowOverlap = true)
		{
			return (U*)(MemoryPool<uint8, CHUNK_SIZE>::AddRange((uint8*)data, size, allowOverlap));
		}

		template<class T>
		T* Add(const T& val, bool allowOverlap = true)
		{
			return static_cast<T*>(Add(&val, sizeof(T), allowOverlap));
		}
	};
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
}
