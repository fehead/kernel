# 최신 ARM 리눅스 커널 4.x 분석

## History

### 2017.01.14
#### Tip
- _mapcount --> page구조체 찾기

#### 진도
- slab_alloc:
```
	slab_alloc_node
		__slab_alloc
			new_slab 라벨 분석.
			new_slab_objects
				get_partial
					get_partial_node
						acquire_slab
						put_cpu_partial
						unfreeze_partials
							discard_slab
								dec_slabs_node
									free_slab
										__free_slab
											__free_pages
					get_any_partial
				new_slab
```

- kmem_cache_free
```
		cache_from_obj	디버그 코드이므로 생략.
		virt_to_head_page
			compound_head_fast

		slab_free
			set_freepointer
			__slab_free
```

### 2017.01.21
#### 문c 블로그
- slub-cache-create

#### 진도
- kmem_cache_create
```
	get_online_cpus
	get_online_mems
	kmem_cache_sanity_check
		probe_kernel_address
	__kmem_cache_alias
		find_mergeable
			slab_unmergeable
		sysfs_slab_alias
	kstrdup_const
		is_kernel_rodata
	kmem_cache_create
		__kmem_cache_create
			do_kmem_cache_create

	put_online_cpus
```

- kmalloc
```
	kmalloc_index
	kmem_cache_alloc_trace
		kmem_cache_alloc
	__kmalloc
		kmalloc_slab
	kmalloc_large
		kmalloc_order_trace
			kmalloc_order
```

- kfree
```
	virt_to_head_page
	__free_kmem_pages - free large page
		__free_pages
	slab_free
```


### 2017.02.04
#### 문c 블로그
- vmalloc
- Red Black Tree Animation
 - https://www.cs.usfca.edu/~galles/visualization/RedBlack.html

#### 진도
- mm_init
```
	percpu_init_late
		pcpu_mem_zalloc
	pgtable_init
		ptlock_cache_init
		pgtable_cache_init
	vmalloc_init
		__insert_vmap_area
			rb_link_node
			rb_insert_color

```

- vmalloc
```
	__vmalloc_node_flags
		__vmalloc_node
			__vmalloc_node_range
				__get_vm_area_node
				alloc_vmap_area
				
```
