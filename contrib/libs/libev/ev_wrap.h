/* DO NOT EDIT, automatically generated by update_ev_wrap */
#ifndef EV_WRAP_H
#define EV_WRAP_H
#define acquire_cb ((loop)->acquire_cb)
#define activecnt ((loop)->activecnt)
#define anfdmax ((loop)->anfdmax)
#define anfds ((loop)->anfds)
#define async_pending ((loop)->async_pending)
#define asynccnt ((loop)->asynccnt)
#define asyncmax ((loop)->asyncmax)
#define asyncs ((loop)->asyncs)
#define backend ((loop)->backend)
#define backend_fd ((loop)->backend_fd)
#define backend_mintime ((loop)->backend_mintime)
#define backend_modify ((loop)->backend_modify)
#define backend_poll ((loop)->backend_poll)
#define checkcnt ((loop)->checkcnt)
#define checkmax ((loop)->checkmax)
#define checks ((loop)->checks)
#define cleanupcnt ((loop)->cleanupcnt)
#define cleanupmax ((loop)->cleanupmax)
#define cleanups ((loop)->cleanups)
#define curpid ((loop)->curpid)
#define epoll_epermcnt ((loop)->epoll_epermcnt)
#define epoll_epermmax ((loop)->epoll_epermmax)
#define epoll_eperms ((loop)->epoll_eperms)
#define epoll_eventmax ((loop)->epoll_eventmax)
#define epoll_events ((loop)->epoll_events)
#define epoll_pessimistic_remove ((loop)->epoll_pessimistic_remove)
#define evpipe ((loop)->evpipe)
#define fdchangecnt ((loop)->fdchangecnt)
#define fdchangemax ((loop)->fdchangemax)
#define fdchanges ((loop)->fdchanges)
#define forkcnt ((loop)->forkcnt)
#define forkmax ((loop)->forkmax)
#define forks ((loop)->forks)
#define fs_2625 ((loop)->fs_2625)
#define fs_fd ((loop)->fs_fd)
#define fs_hash ((loop)->fs_hash)
#define fs_w ((loop)->fs_w)
#define idleall ((loop)->idleall)
#define idlecnt ((loop)->idlecnt)
#define idlemax ((loop)->idlemax)
#define idles ((loop)->idles)
#define invoke_cb ((loop)->invoke_cb)
#define io_blocktime ((loop)->io_blocktime)
#define iocp ((loop)->iocp)
#define iouring_cq_cqes ((loop)->iouring_cq_cqes)
#define iouring_cq_head ((loop)->iouring_cq_head)
#define iouring_cq_overflow ((loop)->iouring_cq_overflow)
#define iouring_cq_ring ((loop)->iouring_cq_ring)
#define iouring_cq_ring_entries ((loop)->iouring_cq_ring_entries)
#define iouring_cq_ring_mask ((loop)->iouring_cq_ring_mask)
#define iouring_cq_ring_size ((loop)->iouring_cq_ring_size)
#define iouring_cq_tail ((loop)->iouring_cq_tail)
#define iouring_entries ((loop)->iouring_entries)
#define iouring_fd ((loop)->iouring_fd)
#define iouring_max_entries ((loop)->iouring_max_entries)
#define iouring_sq_array ((loop)->iouring_sq_array)
#define iouring_sq_dropped ((loop)->iouring_sq_dropped)
#define iouring_sq_flags ((loop)->iouring_sq_flags)
#define iouring_sq_head ((loop)->iouring_sq_head)
#define iouring_sq_ring ((loop)->iouring_sq_ring)
#define iouring_sq_ring_entries ((loop)->iouring_sq_ring_entries)
#define iouring_sq_ring_mask ((loop)->iouring_sq_ring_mask)
#define iouring_sq_ring_size ((loop)->iouring_sq_ring_size)
#define iouring_sq_tail ((loop)->iouring_sq_tail)
#define iouring_sqes ((loop)->iouring_sqes)
#define iouring_sqes_size ((loop)->iouring_sqes_size)
#define iouring_tfd ((loop)->iouring_tfd)
#define iouring_tfd_to ((loop)->iouring_tfd_to)
#define iouring_tfd_w ((loop)->iouring_tfd_w)
#define iouring_to_submit ((loop)->iouring_to_submit)
#define kqueue_changecnt ((loop)->kqueue_changecnt)
#define kqueue_changemax ((loop)->kqueue_changemax)
#define kqueue_changes ((loop)->kqueue_changes)
#define kqueue_eventmax ((loop)->kqueue_eventmax)
#define kqueue_events ((loop)->kqueue_events)
#define kqueue_fd_pid ((loop)->kqueue_fd_pid)
#define linuxaio_ctx ((loop)->linuxaio_ctx)
#define linuxaio_epoll_w ((loop)->linuxaio_epoll_w)
#define linuxaio_iocbpmax ((loop)->linuxaio_iocbpmax)
#define linuxaio_iocbps ((loop)->linuxaio_iocbps)
#define linuxaio_iteration ((loop)->linuxaio_iteration)
#define linuxaio_submitcnt ((loop)->linuxaio_submitcnt)
#define linuxaio_submitmax ((loop)->linuxaio_submitmax)
#define linuxaio_submits ((loop)->linuxaio_submits)
#define loop_count ((loop)->loop_count)
#define loop_depth ((loop)->loop_depth)
#define loop_done ((loop)->loop_done)
#define mn_now ((loop)->mn_now)
#define now_floor ((loop)->now_floor)
#define origflags ((loop)->origflags)
#define pending_w ((loop)->pending_w)
#define pendingcnt ((loop)->pendingcnt)
#define pendingmax ((loop)->pendingmax)
#define pendingpri ((loop)->pendingpri)
#define pendings ((loop)->pendings)
#define periodiccnt ((loop)->periodiccnt)
#define periodicmax ((loop)->periodicmax)
#define periodics ((loop)->periodics)
#define pipe_w ((loop)->pipe_w)
#define pipe_write_skipped ((loop)->pipe_write_skipped)
#define pipe_write_wanted ((loop)->pipe_write_wanted)
#define pollcnt ((loop)->pollcnt)
#define pollidxmax ((loop)->pollidxmax)
#define pollidxs ((loop)->pollidxs)
#define pollmax ((loop)->pollmax)
#define polls ((loop)->polls)
#define port_eventmax ((loop)->port_eventmax)
#define port_events ((loop)->port_events)
#define postfork ((loop)->postfork)
#define preparecnt ((loop)->preparecnt)
#define preparemax ((loop)->preparemax)
#define prepares ((loop)->prepares)
#define release_cb ((loop)->release_cb)
#define rfeedcnt ((loop)->rfeedcnt)
#define rfeedmax ((loop)->rfeedmax)
#define rfeeds ((loop)->rfeeds)
#define rtmn_diff ((loop)->rtmn_diff)
#define sig_pending ((loop)->sig_pending)
#define sigfd ((loop)->sigfd)
#define sigfd_set ((loop)->sigfd_set)
#define sigfd_w ((loop)->sigfd_w)
#define timeout_blocktime ((loop)->timeout_blocktime)
#define timercnt ((loop)->timercnt)
#define timerfd ((loop)->timerfd)
#define timerfd_w ((loop)->timerfd_w)
#define timermax ((loop)->timermax)
#define timers ((loop)->timers)
#define userdata ((loop)->userdata)
#define vec_eo ((loop)->vec_eo)
#define vec_max ((loop)->vec_max)
#define vec_ri ((loop)->vec_ri)
#define vec_ro ((loop)->vec_ro)
#define vec_wi ((loop)->vec_wi)
#define vec_wo ((loop)->vec_wo)
#else
#undef EV_WRAP_H
#undef acquire_cb
#undef activecnt
#undef anfdmax
#undef anfds
#undef async_pending
#undef asynccnt
#undef asyncmax
#undef asyncs
#undef backend
#undef backend_fd
#undef backend_mintime
#undef backend_modify
#undef backend_poll
#undef checkcnt
#undef checkmax
#undef checks
#undef cleanupcnt
#undef cleanupmax
#undef cleanups
#undef curpid
#undef epoll_epermcnt
#undef epoll_epermmax
#undef epoll_eperms
#undef epoll_eventmax
#undef epoll_events
#undef evpipe
#undef fdchangecnt
#undef fdchangemax
#undef fdchanges
#undef forkcnt
#undef forkmax
#undef forks
#undef fs_2625
#undef fs_fd
#undef fs_hash
#undef fs_w
#undef idleall
#undef idlecnt
#undef idlemax
#undef idles
#undef invoke_cb
#undef io_blocktime
#undef iocp
#undef iouring_cq_cqes
#undef iouring_cq_head
#undef iouring_cq_overflow
#undef iouring_cq_ring
#undef iouring_cq_ring_entries
#undef iouring_cq_ring_mask
#undef iouring_cq_ring_size
#undef iouring_cq_tail
#undef iouring_entries
#undef iouring_fd
#undef iouring_max_entries
#undef iouring_sq_array
#undef iouring_sq_dropped
#undef iouring_sq_flags
#undef iouring_sq_head
#undef iouring_sq_ring
#undef iouring_sq_ring_entries
#undef iouring_sq_ring_mask
#undef iouring_sq_ring_size
#undef iouring_sq_tail
#undef iouring_sqes
#undef iouring_sqes_size
#undef iouring_tfd
#undef iouring_tfd_to
#undef iouring_tfd_w
#undef iouring_to_submit
#undef kqueue_changecnt
#undef kqueue_changemax
#undef kqueue_changes
#undef kqueue_eventmax
#undef kqueue_events
#undef kqueue_fd_pid
#undef linuxaio_ctx
#undef linuxaio_epoll_w
#undef linuxaio_iocbpmax
#undef linuxaio_iocbps
#undef linuxaio_iteration
#undef linuxaio_submitcnt
#undef linuxaio_submitmax
#undef linuxaio_submits
#undef loop_count
#undef loop_depth
#undef loop_done
#undef mn_now
#undef now_floor
#undef origflags
#undef pending_w
#undef pendingcnt
#undef pendingmax
#undef pendingpri
#undef pendings
#undef periodiccnt
#undef periodicmax
#undef periodics
#undef pipe_w
#undef pipe_write_skipped
#undef pipe_write_wanted
#undef pollcnt
#undef pollidxmax
#undef pollidxs
#undef pollmax
#undef polls
#undef port_eventmax
#undef port_events
#undef postfork
#undef preparecnt
#undef preparemax
#undef prepares
#undef release_cb
#undef rfeedcnt
#undef rfeedmax
#undef rfeeds
#undef rtmn_diff
#undef sig_pending
#undef sigfd
#undef sigfd_set
#undef sigfd_w
#undef timeout_blocktime
#undef timercnt
#undef timerfd
#undef timerfd_w
#undef timermax
#undef timers
#undef userdata
#undef vec_eo
#undef vec_max
#undef vec_ri
#undef vec_ro
#undef vec_wi
#undef vec_wo
#endif
