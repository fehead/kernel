# 최신 ARM 리눅스 커널 4.x 분석

## History

### 1주차
2017.01.14
- slab_alloc:
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


- kmem_cache_free
		cache_from_obj	디버그 코드이므로 생략.
		virt_to_head_page
			compound_head_fast

		slab_free
			set_freepointer
			__slab_free
				
				
- _mapcount --> page구조체 찾기
