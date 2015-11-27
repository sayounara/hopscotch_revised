#ifndef __HOPSCOTCH_HASHMAP__
#define __HOPSCOTCH_HASHMAP__
////////////////////////////////////////////////////////////////////////////////
// ConcurrentHopscotchHashMap Class
//
////////////////////////////////////////////////////////////////////////////////
//TERMS OF USAGE
//------------------------------------------------------------------------------
//
//	Permission to use, copy, modify and distribute this software and
//	its documentation for any purpose is hereby granted without fee,
//	provided that due acknowledgments to the authors are provided and
//	this permission notice appears in all copies of the software.
//	The software is provided "as is". There is no warranty of any kind.
//
//Authors:
//	Maurice Herlihy
//	Brown University
//	and
//	Nir Shavit
//	Tel-Aviv University
//	and
//	Moran Tzafrir
//	Tel-Aviv University
//
//	Date: July 15, 2008.  
//
////////////////////////////////////////////////////////////////////////////////
// Programmer : Moran Tzafrir (MoranTza@gmail.com)
//
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// INCLUDE DIRECTIVES
////////////////////////////////////////////////////////////////////////////////
#include <stdio.h>
#include <limits.h>
#include "math.h"
#include "memory.h"
#include<iostream>
using namespace std;
////////////////////////////////////////////////////////////////////////////////
// CLASS: ConcurrentHopscotchHashMap
////////////////////////////////////////////////////////////////////////////////
#define  _NULL_DELTA SHRT_MIN 
////////////////////////////////////////////////////////////////////////////////
//INNER CLASSES
////////////////////////////////////////////////////////////////////////////////
class HASH_INT {
public:
	//you must define the following fields and properties
	static const unsigned int _EMPTY_HASH;
	static const unsigned int _BUSY_HASH;
	static const int _EMPTY_KEY;
	static const int _EMPTY_DATA;

	inline static unsigned int Calc(int key) {
		key ^= (key << 15) ^ 0xcd7dcd7d;
		key ^= (key >> 10);
		key ^= (key <<  3);
		key ^= (key >>  6);
		key ^= (key <<  2) + (key << 14);
		key ^= (key >> 16);
		return key;
	}

	inline static bool IsEqual(int left_key, int right_key) {
		return left_key == right_key;
	}

	inline static void relocate_key_reference(int volatile& left, const int volatile& right) {
		left = right;
	}

	inline static void relocate_data_reference(int volatile& left, const int volatile& right) {
		left = right;
	}
};
const unsigned int HASH_INT::_EMPTY_HASH = 0;
const unsigned int HASH_INT::_BUSY_HASH  = 1;
const int HASH_INT::_EMPTY_KEY  = 0;
const int HASH_INT::_EMPTY_DATA = 0;
template <typename	_tKey, 
          typename	_tData,
			 typename	_tHash,
          typename	_tLock,
			 typename	_tMemory>
class HopscotchHashMap {
private:

	// Inner Classes ............................................................
	struct Bucket {
		short				volatile _first_delta;//2个字节，为什么没用unsigned short修饰，因为_first_delta和_next_delta可能为负值。
		short				volatile _next_delta;//2个字节
		unsigned int	volatile _hash;//4个字节
		_tKey				volatile _key;//4个字节
		_tData			volatile _data;//4个字节，所以一个bucket共16字节。
//上述这些字段都用volatile修饰意味着每一次读取都得从内存中load。那么cache岂不是都没发挥作用？在插入和删除操作执行前，都获得了锁，不会出现并发的修改同一个key。可以参照clht的思路，只是在查询的过程中，对加载出来的start_bucket用volatile修饰，这样效果会不会更好？
		void init() {
			_first_delta	= _NULL_DELTA;
			_next_delta		= _NULL_DELTA;
			_hash				= _tHash::_EMPTY_HASH;
			_key				= _tHash::_EMPTY_KEY;
			_data				= _tHash::_EMPTY_DATA;
		}
	};

	struct Segment {
		_u32 volatile	_timestamp;//这一个用volatile修饰的必要性可以很明白地被理解
		_tLock	      _lock;

		void init() {
			_timestamp = 0;
			_lock.init();
		}
	};

	// Fields ...................................................................
	_u32 volatile		_segmentShift;
	_u32 volatile		_segmentMask;
	_u32 volatile		_bucketMask;
	Segment*	volatile	_segments;
	Bucket* volatile	_table;

	const int			_cache_mask;
	const bool			_is_cacheline_alignment;

	// Constants ................................................................
	static const _u32 _INSERT_RANGE  = 1024*4;
	//static const _u32 _NUM_SEGMENTS	= 1024*1;
	//static const _u32 _SEGMENTS_MASK = _NUM_SEGMENTS-1;
	static const _u32 _RESIZE_FACTOR = 2;

	// Small Utilities ..........................................................
	Bucket* get_start_cacheline_bucket(Bucket* const bucket) {
		return (bucket - ((bucket - _table) & _cache_mask)); //can optimize //如果start_buctket正好就是_table[0],通过(bucket - _table) & _cache_mask)可以算出该bucket在一个cache line（0,1,2,3号位置）的第几号位置。然后 (bucket - ((bucket - _table) & _cache_mask))就是一个cache line最开始的存放的bucket。
	}
//	remove_key(segment, start_bucket, curr_bucket, last_bucket, hash);
	void remove_key(Segment&			  segment,
                   Bucket* const		  from_bucket,
						 Bucket* const		  key_bucket, 
						 Bucket* const		  prev_key_bucket, 
						 const unsigned int hash) 
	{
		key_bucket->_hash  = _tHash::_EMPTY_HASH;
		key_bucket->_key   = _tHash::_EMPTY_KEY;
		key_bucket->_data  = _tHash::_EMPTY_DATA;

		if(NULL == prev_key_bucket) {
			if (_NULL_DELTA == key_bucket->_next_delta)
				from_bucket->_first_delta = _NULL_DELTA;
			else 
				from_bucket->_first_delta = (from_bucket->_first_delta + key_bucket->_next_delta);
		} else {
			if (_NULL_DELTA == key_bucket->_next_delta)
				prev_key_bucket->_next_delta = _NULL_DELTA;
			else 
				prev_key_bucket->_next_delta = (prev_key_bucket->_next_delta + key_bucket->_next_delta);
		}

		++(segment._timestamp);
		key_bucket->_next_delta = _NULL_DELTA;
	}
	void add_key_to_begining_of_list(Bucket*	const     keys_bucket, 
										      Bucket*	const		 free_bucket,
												const unsigned int hash,
                                    const _tKey&		 key, 
                                    const _tData& 		 data) 
	{
		free_bucket->_data = data;
		free_bucket->_key  = key;
		free_bucket->_hash = hash;
//一个bucket的_first_delta和_next_delta的区别在哪里？
		if(0 == keys_bucket->_first_delta) {
			if(_NULL_DELTA == keys_bucket->_next_delta)
				free_bucket->_next_delta = _NULL_DELTA;
			else
				free_bucket->_next_delta = (short)((keys_bucket +  keys_bucket->_next_delta) -  free_bucket);
			keys_bucket->_next_delta = (short)(free_bucket - keys_bucket);//假设_NULL_DELTA ！= keys_bucket->_next_delta，178、179两行保证了新插进来的bucket插在start_bucket和原先start_bucket后面那个bucket的中间。
		} else {//第一次插入新的bucket，会执行这个，只有一个start_bucket第一个链接的bucket才会用到_first_delta。
			if(_NULL_DELTA ==  keys_bucket->_first_delta)
				free_bucket->_next_delta = _NULL_DELTA;
			else
				free_bucket->_next_delta = (short)((keys_bucket +  keys_bucket->_first_delta) -  free_bucket);
			keys_bucket->_first_delta = (short)(free_bucket - keys_bucket);
		}
	}

	void add_key_to_end_of_list(Bucket* const      keys_bucket, 
                               Bucket* const		  free_bucket,
                               const unsigned int hash,
                               const _tKey&		  key, 
										 const _tData&		  data,
                               Bucket* const		  last_bucket)
	{
		free_bucket->_data		 = data;
		free_bucket->_key			 = key;
		free_bucket->_hash		 = hash;
		free_bucket->_next_delta = _NULL_DELTA;

		if(NULL == last_bucket)
			keys_bucket->_first_delta = (short)(free_bucket - keys_bucket);
		else 
			last_bucket->_next_delta = (short)(free_bucket - last_bucket);
	}

	void optimize_cacheline_use(Segment& segment, Bucket* const free_bucket) {
		Bucket* const start_cacheline_bucket(get_start_cacheline_bucket(free_bucket));
		Bucket* const end_cacheline_bucket(start_cacheline_bucket + _cache_mask);
		Bucket* opt_bucket(start_cacheline_bucket);

		do {
			if( _NULL_DELTA != opt_bucket->_first_delta) {
				Bucket* relocate_key_last (NULL);
				int curr_delta(opt_bucket->_first_delta);
				Bucket* relocate_key ( opt_bucket + curr_delta);
				do {
					if( curr_delta < 0 || curr_delta > _cache_mask ) {
						_tHash::relocate_data_reference(free_bucket->_data, relocate_key->_data);
						_tHash::relocate_key_reference(free_bucket->_key, relocate_key->_key);
						free_bucket->_hash  = relocate_key->_hash;

						if(_NULL_DELTA == relocate_key->_next_delta)
							free_bucket->_next_delta = _NULL_DELTA;
						else
							free_bucket->_next_delta = (short)( (relocate_key + relocate_key->_next_delta) - free_bucket );

						if(NULL == relocate_key_last)
							opt_bucket->_first_delta = (short)( free_bucket - opt_bucket );
						else
							relocate_key_last->_next_delta = (short)( free_bucket - relocate_key_last );

						++(segment._timestamp);
						relocate_key->_hash			= _tHash::_EMPTY_HASH;
						_tHash::relocate_key_reference(relocate_key->_key, _tHash::_EMPTY_KEY);
						_tHash::relocate_data_reference(relocate_key->_data, _tHash::_EMPTY_DATA);
						relocate_key->_next_delta	= _NULL_DELTA;
						return;
					}

					if(_NULL_DELTA == relocate_key->_next_delta)
						break;
					relocate_key_last = relocate_key;
					curr_delta += relocate_key->_next_delta;
					relocate_key += relocate_key->_next_delta;
				} while(true);//for on list
			}
			++opt_bucket;
		} while (opt_bucket <= end_cacheline_bucket);
	}

public:// Ctors ................................................................
	HopscotchHashMap(
				_u32 inCapacity				= 32*1024*1024,	//init capacity
				_u32 concurrencyLevel	   = 16,			//num of updating threads
				_u32 cache_line_size       = 64,			//Cache-line size of machine
				bool is_optimize_cacheline = true)		
	:	_cache_mask					( (cache_line_size / sizeof(Bucket)) - 1 ),//_cache_mask=64/16-1=3，二进制表示（0011）
		_is_cacheline_alignment	( is_optimize_cacheline ),
		_segmentMask  ( NearestPowerOfTwo(concurrencyLevel) - 1),
		_segmentShift ( CalcDivideShift(NearestPowerOfTwo(concurrencyLevel/(NearestPowerOfTwo(concurrencyLevel)))-1) )
	{
		//ADJUST INPUT ............................
		const _u32 adjInitCap = NearestPowerOfTwo(inCapacity);
		const _u32 adjConcurrencyLevel = NearestPowerOfTwo(concurrencyLevel);
		const _u32 num_buckets( adjInitCap + _INSERT_RANGE + 1);
		_bucketMask = adjInitCap - 1;
		_segmentShift = first_msb_bit_indx(_bucketMask) - first_msb_bit_indx(_segmentMask);

		//ALLOCATE THE SEGMENTS ...................
		_segments = (Segment*) _tMemory::byte_aligned_malloc( (_segmentMask + 1) * sizeof(Segment) );
		_table = (Bucket*) _tMemory::byte_aligned_malloc( num_buckets * sizeof(Bucket) );

		Segment* curr_seg = _segments;
		for (_u32 iSeg = 0; iSeg <= _segmentMask; ++iSeg, ++curr_seg) {
			curr_seg->init();
		}

		Bucket* curr_bucket = _table;
		for (_u32 iElm=0; iElm < num_buckets; ++iElm, ++curr_bucket) {
			curr_bucket->init();
		}
	}

	~HopscotchHashMap() {
		_tMemory::byte_aligned_free(_table);
		_tMemory::byte_aligned_free(_segments);
	} 

	// Query Operations .........................................................
	inline_ bool containsKey( const _tKey& key ) {

		//CALCULATE HASH ..........................
		const unsigned int hash( _tHash::Calc(key) );

		//CHECK IF ALREADY CONTAIN ................
		const	Segment&	segment(_segments[(hash >> _segmentShift) & _segmentMask]);//(hash >> _segmentShift) & _segmentMask这个值的最大范围是在0～_segmentMask（线程数-1）之间，

     //go over the list and look for key
		unsigned int start_timestamp;
      do {
			start_timestamp = segment._timestamp;
			const Bucket* curr_bucket( &(_table[hash & _bucketMask]) );
			short next_delta( curr_bucket->_first_delta );
			//也可能在while循环之前，也即时执行到这个位置的时候，发生了一个concurrent remove。concurrent remove之后。segment._timestamp会发生变化，
         while( _NULL_DELTA != next_delta ) {
				curr_bucket += next_delta;
				if(hash == curr_bucket->_hash && _tHash::IsEqual(key, curr_bucket->_key))
					return true;
				next_delta = curr_bucket->_next_delta;
			}//如果进行了第一遍while循环（此处用 do...while循环的好处就在这），没有找到这个key。恰在这时，一个concurrent insert（但是好像insert操作的过程中，segment._timestamp不会发生变化，这也符合常理，毕竟这个插入，已经是在执行了查询操作之后发生的（里面的while循环算作查询主体部分））操作插入了寻找的这个key，segment._timestamp不会变化（insert方法没有对segment._timestamp修改），如果变化的话，从而判断不相等，进行下一次遍历寻找，而找到了。这会造成典型的ABA问题（有一个时间先后顺序来确定），所以在insert方法没有对segment._timestamp修改。
		} while(start_timestamp != segment._timestamp);//如果两个start_timestamp == segment._timestamp表示在这个过程中没有发生一个concurrent insert插入了要寻找的这个key值，从而完全可以判定这次查找失败，即hashtable中不存在这个key值

		return false;
	}

	//插入成功返回0，插入失败返回key的value值。
	//modification Operations ...................................................
	inline_ _tData putIfAbsent(const _tKey& key, const _tData& data) {
		const unsigned int hash( _tHash::Calc(key) );
		Segment&	segment(_segments[(hash >> _segmentShift) & _segmentMask]);

		//go over the list and look for key
		segment._lock.lock();
		Bucket* const start_bucket( &(_table[hash & _bucketMask]) );

		Bucket* last_bucket( NULL );
		Bucket* compare_bucket( start_bucket );
		short next_delta( compare_bucket->_first_delta );
		while (_NULL_DELTA != next_delta) {
			compare_bucket += next_delta;
			if( hash == compare_bucket->_hash && _tHash::IsEqual(key, compare_bucket->_key) ) {
				const _tData rc((_tData&)(compare_bucket->_data));
				segment._lock.unlock();
				return rc;//要插入的key已经存在，插入失败，返回key对应的value值。
			}
			last_bucket = compare_bucket;
			next_delta = compare_bucket->_next_delta;
		}
//进入到下面，说明要插入的key在hashtable中不存在
		//try to place the key in the same cache-line
		if(_is_cacheline_alignment) {
			Bucket*	free_bucket( start_bucket );
			Bucket*	start_cacheline_bucket(get_start_cacheline_bucket(start_bucket));
			Bucket*	end_cacheline_bucket(start_cacheline_bucket + _cache_mask);
			do {
				if( _tHash::_EMPTY_HASH == free_bucket->_hash ) {//在start_bucket所在的这一行cache line找到了一个 empty slot。
					add_key_to_begining_of_list(start_bucket, free_bucket, hash, key, data);
					segment._lock.unlock();
					return _tHash::_EMPTY_DATA;
				}
				++free_bucket;
				//下面的if判断保证了start_bucket所在的这一个cache line存放的四个bucket都能被遍历到。
				if(free_bucket > end_cacheline_bucket)
					free_bucket = start_cacheline_bucket;
			} while(start_bucket != free_bucket);
		}

		//place key in arbitrary free forward bucket
		Bucket* max_bucket( start_bucket + (SHRT_MAX-1) );
		Bucket* last_table_bucket(_table + _bucketMask);
		if(max_bucket > last_table_bucket)
			max_bucket = last_table_bucket;
		Bucket* free_max_bucket( start_bucket + (_cache_mask + 1) );
		while (free_max_bucket <= max_bucket) {
			if( _tHash::_EMPTY_HASH == free_max_bucket->_hash ) {
				add_key_to_end_of_list(start_bucket, free_max_bucket, hash, key, data, last_bucket);
				segment._lock.unlock();
				return _tHash::_EMPTY_DATA;
			}
			++free_max_bucket;
		}

		//place key in arbitrary free backward bucket
		Bucket* min_bucket( start_bucket - (SHRT_MAX-1) );
		if(min_bucket < _table)
			min_bucket = _table;
		Bucket* free_min_bucket( start_bucket - (_cache_mask + 1) );
		while (free_min_bucket >= min_bucket) {
			if( _tHash::_EMPTY_HASH == free_min_bucket->_hash ) {
				add_key_to_end_of_list(start_bucket, free_min_bucket, hash, key, data, last_bucket);
				segment._lock.unlock();
				return _tHash::_EMPTY_DATA;
			}
			--free_min_bucket;
		}
//		cout<<"putifabsent"<<endl;
		//NEED TO RESIZE ..........................
		fprintf(stderr, "ERROR - RESIZE is not implemented - size %u\n", size());
		exit(1);
		return _tHash::_EMPTY_DATA;
	}

	//删除成功返回原key的value值，删除失败返回0；
	inline_ _tData remove(const _tKey& key) {
		//CALCULATE HASH ..........................
		const unsigned int hash( _tHash::Calc(key) );

		//CHECK IF ALREADY CONTAIN ................
		Segment&	segment(_segments[(hash >> _segmentShift) & _segmentMask]);
		segment._lock.lock();
		Bucket* const start_bucket( &(_table[hash & _bucketMask]) );
		Bucket* last_bucket( NULL );
		Bucket* curr_bucket( start_bucket );
		short	  next_delta (curr_bucket->_first_delta);
//		cout<<"removeStart"<<endl;
		do {
			if(_NULL_DELTA == next_delta) {
				segment._lock.unlock();
				return _tHash::_EMPTY_DATA;
			}
			curr_bucket += next_delta;

			if( hash == curr_bucket->_hash && _tHash::IsEqual(key, curr_bucket->_key) ) {
				_tData const rc((_tData&)(curr_bucket->_data));
				remove_key(segment, start_bucket, curr_bucket, last_bucket, hash);
				if( _is_cacheline_alignment )
					optimize_cacheline_use(segment, curr_bucket);
				segment._lock.unlock();
				return rc;
			}
			last_bucket = curr_bucket;
			next_delta = curr_bucket->_next_delta;
		} while(true);
//		cout<<"remove return value "<<_tHash::_EMPTY_DATA<<endl;

		return _tHash::_EMPTY_DATA;
	}

	//status Operations .........................................................
	unsigned int size() {
		_u32 counter = 0;
		const _u32 num_elm( _bucketMask + _INSERT_RANGE );
		for(_u32 iElm=0; iElm < num_elm; ++iElm) {
			if( _tHash::_EMPTY_HASH != _table[iElm]._hash ) {
				++counter;
			}
		}
		return counter;
	}   

	double percentKeysInCacheline() {
		unsigned int total_in_cache( 0 );
		unsigned int total( 0 );

		Bucket* curr_bucket(_table);
		for(int iElm(0); iElm <= _bucketMask; ++iElm, ++curr_bucket) {

			if(_NULL_DELTA != curr_bucket->_first_delta) {
				Bucket* const startCacheLineBucket( get_start_cacheline_bucket(curr_bucket) );
				Bucket* check_bucket(curr_bucket + curr_bucket->_first_delta);
				int currDist( curr_bucket->_first_delta );
				do {
					++total;
					if( (check_bucket - startCacheLineBucket)  >= 0 && (check_bucket - startCacheLineBucket) <= _cache_mask )
						++total_in_cache;
					if(_NULL_DELTA == check_bucket->_next_delta)
						break;
					currDist += check_bucket->_next_delta;
					check_bucket += check_bucket->_next_delta;
				} while(true);
			}
		}

		//return percent in cache
		return (((double)total_in_cache)/((double)total)*100.0);
	}

private:
	// Private Static Utilities .................................................
	static _u32 NearestPowerOfTwo(const _u32 value)	{
		_u32 rc( 1 );
		while (rc < value) {
			rc <<= 1;
		}
		return rc;
	}

	static unsigned int CalcDivideShift(const unsigned int _value) {
		unsigned int numShift( 0 );
		unsigned int curr( 1 );
		while (curr < _value) {
			curr <<= 1;
			++numShift;
		}
		return numShift;
	}

};

#endif
