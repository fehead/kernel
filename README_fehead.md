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
#### 참고
- http://jake.dothome.co.kr/slub-cache-create

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
#### 참고
- http://jake.dothome.co.kr/vmalloc
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

### 2017.02.11
#### 참고
- http://jake.dothome.co.kr/vmalloc

#### 진도
- vmalloc
```
					setup_vmalloc_vm
				__vmalloc_area_node
					get_vm_area_size
					map_vm_area
						vmap_page_range
							vmap_page_range_noflush
								vmap_pud_range
									pud_alloc
									vmap_pmd_range
										pmd_alloc
										pmd_addr_end
										vmap_pte_range
											pte_alloc_kernel
												__pte_alloc_kernel
													pte_alloc_one_kernel
														__get_free_page
														clean_pte_table
											set_pte_at
												__sync_icache_dcache
												set_pte_ext



							flush_cache_vmap
```

 - vmap
```
	get_vm_area_caller
	map_vm_area
```

 - vfree
```
	__vunmap(addr, 1)
```

 - vunmap
```
	__vunmap(addr, 0)
		remove_vm_area
			find_vmap_area
			free_unmap_vmap_area
				flush_cache_vunmap
				free_unmap_vmap_area_noflush
					unmap_vmap_area
						vunmap_page_range
							pgd_none_or_clear_bad
							vunmap_pud_range
								vunmap_pmd_range
									vunmap_pte_range
										pte_offset_kernel
										ptep_get_and_clear
					free_vmap_area_noflush
						lazy_max_pages
						try_purge_vmap_area_lazy
							__purge_vmap_area_lazy
								__free_vmap_area
								flush_tlb_kernel_range
							
```

### 2017.02.18

#### 진도
- __vectors_start
```
	vector_rst
	vector_und
	__stubs_start
		vector_swi (arch/arm/kernel/entry-common.S)
			
```

- vector_pabt
```
	vector_stub - vector_stub pabt, ABT_MODE, 4
	__pabt_usr
		usr_entry
		pabt_helper
			v7_pabort
				do_PrefetchAbort
```

### 2017.02.25

#### 진도
- vector_pabt
```
		usr_entry
		pabt_helper
			v7_pabort
				do_PrefetchAbort
					arm_notify_die
						die
		ret_from_exception
			get_thread_info
			ret_to_user
				work_pending
					do_work_pending
					no_work_pending
				restore_user_regs
```
		
### 2017.03.04

#### 진도
- start_kernel -> time_init -> of_clk_init
```
	CLK_OF_DECLARE
		OF_DECLARE_1
			_OF_DECLARE
	for_each_matching_node_and_match
	parent_ready
		__of_clk_get
			of_parse_phandle_with_args

```

### 2017.03.11

#### 참고
- http://jake.dothome.co.kr/clk-1

#### 진도
- of_fixed_clk_setup
```
	clk_register_fixed_rate_with_accuracy
		clk_register
			__clk_create_clk
			__clk_init
				clk_core_lookup
					__clk_lookup_subtree
				__clk_init_parent
					clk_core_get_parent_by_index
				clk_core_reparent
					clk_reparent
					__clk_recalc_accuracies
					__clk_recalc_rates
						__clk_notify

```

### 2017.03.11
#### 참고
- http://jake.dothome.co.kr/clk-1/
- http://jake.dothome.co.kr/clk-2/

#### 진도
- of_fixed_factor_clk_setup
```
	of_clk_get_parent_name
		of_parse_phandle_with_args
	clk_register_fixed_factor
		clk_register


```

### 2017.03.18
#### 참고
- http://jake.dothome.co.kr/clk-1/

#### 진도
- of_clk_set_defaults
```
	__set_clk_parents
	__set_clk_rates
		clk_set_rate
			clk_core_set_rate_nolock
				clk_calc_new_rates
					clk_core_get_boundaries
					clk_fetch_parent_index
					clk_calc_subtree
				clk_propagate_rate_change
				clk_change_rate
					__clk_set_parent_before
					__clk_set_parent_after
					clk_change_rate
```

- clk_notifier_register
```
```

### 2017.04.01
#### 참고
- http://elinux.org/images/b/b8/Elc2013_Clement.pdf
- http://jake.dothome.co.kr/

#### 진도
- early_irq_init
```
	init_irq_default_affinity
	arch_probe_nr_irqs
	alloc_desc
		alloc_masks
			zalloc_cpumask_var_node
		desc_set_defaults
			irq_settings_clr_and_set
			irqd_set
			desc_smp_init
	irq_insert_desc
```

- init_IRQ
```
	bcm2709_init_irq
		armctrl_init
	irqchip_init
		of_irq_init
			of_irq_find_parent
			bcm2836_arm_irqchip_l1_intc_of_init
			bcm2836_armctrl_of_init
			gic_of_init
				of_iomap
					of_address_to_resource
						of_get_address
```

### 2017.04.08
#### 참고
- http://jake.dothome.co.kr/ic
- http://jake.dothome.co.kr/interrupts-2
- http://jake.dothome.co.kr/interrupts-3

#### 진도
```
						__of_address_to_resource
							of_translate_address
								__of_translate_address
									of_translate_one
				gic_init_bases
					readl_relaxed

```

### 2017.04.15
#### 참고
- http://jake.dothome.co.kr/interrupts-3

#### 진도
- gic_init_bases
```
	irq_domain_add_linear
		__irq_domain_add
	gic_dist_init
	gic_cpu_init
		gic_get_cpumask
		gic_cpu_config
```

### 2017.04.22
#### 참고
- http://jake.dothome.co.kr
- http://forum.falinux.com/zbxe/index.php?document_srl=784699&mid=lecture_tip

#### 진도
- gic_of_init -> irq_of_parse_and_map
```
	of_irq_parse_one
		of_irq_parse_raw
	irq_create_of_mapping
		irq_find_host

		irq_find_mapping
			irq_domain_get_irq_data
				irq_get_irq_data
					irq_to_desc
		irq_domain_alloc_irqs
			__irq_domain_alloc_irqs
				irq_domain_alloc_descs
				irq_domain_alloc_irq_data
						irq_domain_insert_irq_data
						irq_domain_alloc_irqs_recursive
gic_cascade_irq
	irq_set_handler_data
		


```

### 2017.04.29
#### 참고
- http://jake.dothome.co.kr/interrupts-2

#### 진도
- gic_of_init -> irq_of_parse_and_map
```
							gic_irq_domain_alloc
								gic_irq_domain_xlate
								gic_irq_domain_map
									irq_set_percpu_devid
						irq_domain_insert_irq
		
		irq_create_mapping
			irq_domain_associate
				gic_irq_domain_map
		irq_get_trigger_type
		irq_set_irq_type
			__irq_set_trigger
				gic_set_type
				unmask_irq
					gic_unmask_irq

```

- gic_handle_irq
```
	__handle_domain_irq
		generic_handle_irq
			generic_handle_irq_desc
```

### 2017.05.06
#### 참고
- arch/arm/boot/dts/rk3288.dtsi arm,gic-400

#### 진도
- init_IRQ
```
of_irq_init
```

### 2017.05.13
#### 참고
- http://jake.dothome.co.kr/interrupts-2
- http://jake.dothome.co.kr/two-part-interrupt-handler
- http://jake.dothome.co.kr/softirq

#### 진도
- softirq_init
```
```

- __do_softirq
```
	local_softirq_pending
```

- raise_softirq
```
	raise_softirq_irqoff
		__raise_softirq_irqoff
```

### 2017.05.20
#### 참고
- http://jake.dothome.co.kr/ipi-cross-call/
- http://jake.dothome.co.kr/lowrestimer/

#### 진도
- smp_call_function_many
- smp_cross_call
- do_IPI -> handle_IPI
```
	tick_receive_broadcast
	generic_smp_call_function_interrupt
```

- init_timers - timer_cpu_notify - init_timers_cpu
- add_timer
```
	mod_timer
		apply_slack
		timer_pending
		__mod_timer
			detach_if_pending
```

### 2017.05.27
#### 참고

#### 진도
- add_timer -> mod_timer -> __mod_timer
```
			get_nohz_timer_target
			internal_add_timer
				__internal_add_timer
```
- run_timer_softirq -> __run_timers
```
	cascade
```
