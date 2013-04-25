
/*
 * OS X ZFS vnode operation wrappers.
 *
 * The argument structure layouts were obtained from:
 * http://www.opensource.apple.com/source/xnu/xnu-792.13.8/bsd/vfs/vfs_support.c
 * http://code.ohloh.net/project?pid=Ybsxw4FOQb8
 *
 * This file should contain primarily interface points; if an interface
 * definition is more than 100 lines long, parts of it should be refactored
 * into zfs_vnops_osx_lib.c.
 */

/*
 * XXX GENERAL COMPATIBILITY ISSUES
 *
 * 'name' is a common argument, but in OS X (and FreeBSD), we need to pass
 * the componentname pointer, so other things can use them.  We should
 * change the 'name' argument to be an opaque name pointer, and define
 * OS-dependent macros that yield the desired results when needed.
 *
 * On OS X, VFS performs access checks before calling anything, so
 * zfs_zaccess_* calls are not used.  Not true on FreeBSD, though.  Perhaps
 * those calls should be conditionally #if 0'd?
 *
 * On OS X, VFS & I/O objects are often opaque, e.g. uio_t and vnode_t
 * require using functions to access elements of an object.  Should convert
 * the Solaris code to use macros on other platforms.
 *
 * OS X and FreeBSD appear to use similar zfs-vfs interfaces; see Apple's
 * comment in zfs_remove() about the fact that VFS holds the last ref while
 * in Solaris it's the ZFS code that does.  On FreeBSD, the code Apple
 * refers to here results in a panic if the branch is actually taken.
 *
 * OS X uses vnode_put() in place of VN_RELE - needs a #define?
 */

#define	DECLARE_CRED(ap) \
	cred_t *cr = (cred_t *)vfs_context_ucred((ap)->a_context)
#define	DECLARE_CONTEXT(ap) \
	caller_context_t *ct = (caller_context_t *)(ap)->a_context
#define	DECLARE_CRED_AND_CONTEXT(ap)	\
	DECLARE_CRED(ap);		\
	DECLARE_CONTEXT(ap)


static int
zfs_vnop_open(ap)
	struct vnop_open_args /* {
		struct vnode *a_vp;
		int a_mode;
		vfs_context_t a_context;
	} */ *ap;
{
	DECLARE_CRED_AND_CONTEXT(ap);

	return (zfs_open(&ap->a_vp, ap->a_mode, cr, ct));
}

static int
zfs_vnop_close(ap)
	struct vnop_close_args /* {
		struct vnode *a_vp;
		int a_fflag;
		vfs_context_t a_context;
	} */ *ap;
{
	int count = 1;
	int offset = 0;
	DECLARE_CRED_AND_CONTEXT(ap);

	return (zfs_close(ap->a_vp, ap->a_fflag, count, offset, cr, ct));
}

static int
zfs_vnop_ioctl(ap)
	struct vnop_ioctl_args /* {
		struct vnode *a_vp;
		u_long a_command;
		caddr_t a_data;
		int a_fflag;
		kauth_cred_t a_cred;
		struct proc *a_p;
	} */ *ap;
{
	/* OS X has no use for zfs_ioctl(). */
	znode_t *zp = VTOZ(ap->a_vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	user_addr_t useraddr = CAST_USER_ADDR_T(ap->a_data);
	int error;

	ZFS_ENTER(zfsvfs);

	switch(ap->a_command) {
	case F_FULLFSYNC:
		error = zfs_vnop_ioctl_fsync(ap->a_vp, ap->a_context, zfsvfs);
		break;
	case SPOTLIGHT_GET_MOUNT_TIME:
		error = copyout(&zfsvfs->z_mount_time, useraddr,
		    sizeof(zfsvfs->z_mount_time));
		break;
	case SPOTLIGHT_GET_UNMOUNT_TIME:
		error = copyout(&zfsvfs->z_last_unmount_time, useraddr,
		    sizeof(zfsvfs->z_last_unmount_time));
		break;
	default:
		error = ENOTTY;
	}

	ZFS_EXIT(zfsvfs);
	return (error);
}

static int
zfs_vnop_read(ap)
	struct vnop_read_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		int a_ioflag;
		vfs_context_t a_context;
	} */ *ap;
{
	int ioflag = zfs_ioflags(ap->a_ioflag);
	DECLARE_CRED_AND_CONTEXT(ap);

	return (zfs_read(ap->a_vp, ap->a_uio, ioflag, cr, ct));
}

static int
zfs_vnop_write(ap)
	struct vnop_write_args /* {
		struct vnode *a_vp;
		struct uio *a_uio;
		int a_ioflag;
		vfs_context_t a_context;
	} */ *ap;
{
	int ioflag = zfs_ioflags(ap->a_ioflag);
	DECLARE_CRED_AND_CONTEXT(ap);

	return (zfs_write(ap->a_vp, ap->a_uio, ioflag, cr, ct));
}

static int
zfs_vnop_access(ap)
	struct vnop_access_args /* {
		struct vnode *a_vp;
		int a_mode;
		vfs_context_t a_context;
	} */ *ap;
{
	int error;
	int mode = ap->a_mode;

	error = zfs_access_native_mode(ap->a_vp, &mode, ap->a_cred,
	    ap->a_context);

	/* XXX Check for other modes? */

	return (error);
}

static int
zfs_vnop_lookup(struct vnop_lookup_args *ap)
	struct vnop_lookup_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
		vfs_context_t a_context;
	} */ *ap;
{
	struct componentname *cnp = ap->a_cnp;
	DECLARE_CRED(ap);
	int error;

	error = zfs_lookup(ap->a_dvp, cnp, ap->a_vpp,
	    /*pnp*/NULL, /*flags*/0, /*rdir*/NULL, cr, ap->a_context,
	    /*direntflags*/NULL, /*realpnp*/NULL);

	/* XXX FreeBSD has some namecache stuff here. */

	return (error);
}

static int
zfs_vnop_create(ap)
	struct vnop_create_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
		struct vnode_vattr *a_vap;
		vfs_context_t a_context;
	} */ *ap;
{
	struct componentname *cnp = ap->a_cnp;
	vattr_t *vap = ap->a_vap;
	DECLARE_CRED(ap);
	vcexcl_t excl;

	excl = (vap->va_vaflags & VA_EXCLUSIVE) ? EXCL : NONEXCL;

	return (zfs_create(ap->a_dvp, cnp, vap, excl, mode, ap->a_vpp, cr,
	    /*flag*/0, ap->a_context, /*vsecp*/NULL));
}

static int
zfs_vnop_remove(ap)
	struct vnop_remove_args /* {
		struct vnode *a_dvp;
		struct vnode *a_vp;
		struct componentname *a_cnp;
		int a_flags;
		vfs_context_t a_context;
	} */ *ap;
{
	DECLARE_CRED_AND_CONTEXT(ap);

	return (zfs_remove(ap->a_dvp, ap->a_cnp, cr, ct, /*flags*/0));
}

static int
zfs_vnop_mkdir(ap)
	struct vnop_mkdir_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
		struct vnode_vattr *a_vap;
		vfs_context_t a_context;
	} */ *ap;
{
	DECLARE_CRED_AND_CONTEXT(ap);

	return (zfs_mkdir(ap->a_dvp, ap->a_cnp, vap, ap->a_vpp,
	    cr, ct, /*flags*/0, /*vsecp*/NULL));
}

static int
zfs_vnop_rmdir(ap)
	struct vnop_rmdir_args /* {
		struct vnode *a_dvp;
		struct vnode *a_vp;
		struct componentname *a_cnp;
		vfs_context_t a_context;
	} */ *ap;
{
	DECLARE_CRED_AND_CONTEXT(ap);

	return (zfs_rmdir(ap->a_dvp, ap->a_cnp, /*cwd*/NULL,
	    cr, ct, /*flags*/0));
}

static int
zfs_vnop_readdir(ap)
	struct vnop_readdir_args /* {
		vnode_t a_vp;
		struct uio *a_uio;
		int a_flags;
		int *a_eofflag;
		int *a_numdirent;
		vfs_context_t a_context;
	} */ *ap;
{
	DECLARE_CRED(cr);
	/*
	 * XXX This interface needs vfs_has_feature.
	 * XXX zfs_readdir() also needs to grow support for passing back the
	 *     number of entries (OSX/FreeBSD) and cookies (FreeBSD).
	 *     However, it should be the responsibility of the OS caller to
	 *     malloc/free space for that.
	 */

	ap->a_numdirent = 0;
	return (zfs_readdir(ap->a_vp, ap->a_uio, cr, ap->a_eofflag,
	    ap->a_numdirent, /*a_cookies*/NULL));
}

static int
zfs_vnop_fsync(ap)
	struct vnop_fsync_args /* {
		struct vnode *a_vp;
		int a_waitfor;
		vfs_context_t a_context;
	} */ *ap;
{
	znode_t *zp = VTOZ(ap->a_vp);
	DECLARE_CRED_AND_CONTEXT(ap);

	/*
	 * Check if this znode has already been synced, freed, and recycled
	 * by znode_pageout_func.
	 *
	 * XXX: What is this?  Substitute for Illumos vn_has_cached_data()?
	 */
	if (zp == NULL)
		return (0);

	return (zfs_fsync(ap->a_vp, /*flag*/0, cr, ct));
}

static int
zfs_vnop_getattr(ap)
	struct vnop_getattr_args /* {
		struct vnode *a_vp;
		struct vnode_vattr *a_vap;
		vfs_context_t a_context;
	} */ *ap;
{
	DECLARE_CRED_AND_CONTEXT(ap);
	/*
	 * XXX This one requires modifying zfs_getattr(), unfortunately.
	 *     There are two parts: the first section where we fill in vap
	 *     from the znode, while holding its lock.  The second section
	 *     where we fill in other stuff from the znode's dbuf and objset
	 *     which doesn't require the znode lock.
	 */

	return (zfs_getattr(ap->a_vp, ap->a_vap, /*flags*/0, cr, ct));
}

static int
zfs_vnop_setattr(ap)
	struct vnop_setattr_args /* {
		struct vnode *a_vp;
		struct vnode_vattr *a_vap;
		vfs_context_t a_context;
	} */ *ap;
{
	DECLARE_CRED_AND_CONTEXT(ap);

	return (zfs_setattr(ap->a_vp, ap->a_vap, /*flag*/0, cr, ct));
}

static int
zfs_vnop_rename(ap)
	struct vnop_rename_args /* {
		struct vnode *a_fdvp;
		struct vnode *a_fvp;
		struct componentname *a_fcnp;
		struct vnode *a_tdvp;
		struct vnode *a_tvp;
		struct componentname *a_tcnp;
		vfs_context_t a_context;
	} */ *ap;
{
	DECLARE_CRED_AND_CONTEXT(ap);
	int error;

	error = zfs_rename(ap->a_fdvp, ap->a_fcnp, ap->a_tdvp, ap->a_tcnp,
	    cr, ct);

	/* Remove entries from the namei cache. */
	cache_purge(ap->a_tdvp);

	return (error);
}

static int
zfs_vnop_symlink(ap)
	struct vnop_symlink_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
		struct vnode_vattr *a_vap;
		char *a_target;
		vfs_context_t a_context;
	} */ *ap;
{
	DECLARE_CRED(ap);
	int error;

	/* OS X doesn't need to set vap->va_mode? */
	error = zfs_symlink(ap->a_dvp, ap->a_cnp, ap->a_vap, ap->a_target, cr);

	/* XXX zfs_attach_vnode()? */

	return (error);
}

static int
zfs_vnop_readlink(ap)
	struct vnop_readlink_args /* {
		struct vnode *vp;
		struct uio *uio;
		vfs_context_t a_context;
	} */ *ap;
{
	DECLARE_CRED(ap);

	return (zfs_readlink(ap->a_vp, ap->a_uio, cr));
}

static int
zfs_vnop_link(ap)
	struct vnop_link_args /* {
		struct vnode *a_vp;
		struct vnode *a_tdvp;
		struct componentname *a_cnp;
		vfs_context_t a_context;
	} */ *ap;
{
	DECLARE_CRED(ap);

	/* XXX Translate this inside zfs_link() instead. */
	if (vnode_mount(ap->a_vp) != vnode_mount(ap->a_tdvp))
		return (EXDEV);


	/*
	 * XXX Understand why Apple made this comparison in so many places
	 * where others do not.
	 */
	if (cnp->cn_namelen >= ZAP_MAXNAMELEN)
		return (ENAMETOOLONG);

	return (zfs_link(ap->a_tdvp, ap->a_vp, ap->a_cnp, cr));
}

static int
zfs_vnop_pagein(ap)
	struct vnop_pagein_args /* {
		struct vnode *a_vp;
		upl_t a_pl;
		vm_offset_t a_pl_offset;
		off_t a_foffset;
		size_t a_size;
		int a_flags;
		vfs_context_t a_context;
	} */ *ap;
{

	/* XXX Crib this from the Apple zfs_vnops.c. */
	return (0);
}

static int
zfs_vnop_pageout(ap)
	struct vnop_pageout_args /* {
		struct vnode *a_vp;
		upl_t a_pl;
		vm_offset_t a_pl_offset;
		off_t a_foffset;
		size_t a_size;
		int a_flags;
		vfs_context_t a_context;
	} */ *ap;
{

	/*
	 * XXX Crib this too, although Apple uses parts of zfs_putapage().
	 * Break up that function into smaller bits so it can be reused.
	 */
	return (zfs_putapage());
}

static int
zfs_vnop_mmap(ap)
	struct vnop_mmap_args /* {
		struct vnode *a_vp;
		int a_fflags;
		kauth_cred_t a_cred;
		struct proc *a_p;
	} */ *ap;
{

	return (0); /* zfs_mmap? */
}

static int
zfs_vnop_inactive(ap)
	struct vnop_inactive_args /* {
		struct vnode *a_vp;
		vfs_context_t a_context;
	} */ *ap;
{
}

static int
zfs_vnop_reclaim(ap)
	struct vnop_reclaim_args /* {
		struct vnode *a_vp;
		vfs_context_t a_context;
	} */ *ap;
{
}

static int
zfs_vnop_mknod(ap)
	struct vnop_mknod_args /* {
		struct vnode *a_dvp;
		struct vnode **a_vpp;
		struct componentname *a_cnp;
		struct vnode_vattr *vap;
		vfs_context_t a_context;
	} */ *ap;
{

	return (zfs_vnop_create((struct vnop_create_args *)ap));
}

static int
zfs_vnop_allocate(ap)
	struct vnop_allocate_args /* {
		struct vnode *a_vp;
		off_t a_length;
		u_int32_t a_flags;
		off_t *a_bytesallocated;
		off_t a_offset;
		vfs_context_t a_context;
	} */ *ap;
{

	return (0);
}

static int
zfs_vnop_whiteout(ap)
	struct vnop_whiteout_args /* {
		struct vnode *a_dvp;
		struct componentname *a_cnp;
		int a_flags;
		vfs_context_t a_context;
	} */ *ap;
{

	return (0);
}

static int
zfs_vnop_pathconf(ap)
	struct vnop_pathconf_args /* {
		struct vnode *a_vp;
		int a_name;
		register_t *a_retval;
		vfs_context_t a_context;
	} */ *ap;
{

	return (0);
}

static int
zfs_vnop_getxattr(ap)
	struct vnop_getxattr_args /* {
		struct vnodeop_desc *a_desc;
		struct vnode *a_vp;
		char *a_name;
		struct uio *a_uio;
		size_t *a_size;
		int a_options;
		vfs_context_t a_context;
	} */ *ap;
{

	return (0);
}

static int
zfs_vnop_setxattr(ap)
	struct vnop_setxattr_args /* {
		struct vnodeop_desc *a_desc;
		struct vnode *a_vp;
		char *a_name;
		struct uio *a_uio;
		int a_options;
		vfs_context_t a_context;
	} */ *ap;
{

	return (0);
}

static int
zfs_vnop_removexattr(ap)
	struct vnop_removexattr_args /* {
		struct vnodeop_desc *a_desc;
		struct vnode *a_vp;
		char *a_name;
		int a_options;
		vfs_context_t a_context;
	} */ *ap;
{

	return (0);
}

static int
zfs_vnop_listxattr(ap)
	struct vnop_listxattr_args /* {
		struct vnodeop_desc *a_desc;
		struct vnode *a_vp;
		char *a_name;
		struct uio *a_uio;
		size_t *a_size;
		int a_options;
		vfs_context_t a_context;
	} */ *ap;
{

	return (0);
}

#ifdef HAVE_NAMED_STREAMS
static int
zfs_vnop_getnamedstream(ap)
	struct vnop_getnamedstream_args /* {
		struct vnode *a_vp;
		struct vnode **a_svpp;
		char *a_name;
	} */ *ap;
{

	return (0);
}

static int
zfs_vnop_makenamedstream_args(ap)
	struct vnop_makenamedstream_args /* {
		struct vnode *a_vp;
		struct vnode **a_svpp;
		char *a_name;
	} */ *ap;
{

	return (0);
}

static int
zfs_vnop_removenamedstream(ap)
	struct vnop_removenamedstream_args /* {
		struct vnode *a_vp;
		struct vnode **a_svpp;
		char *a_name;
	} */ *ap;
{

	return (0);
}
#endif /* HAVE_NAMED_STREAMS */

static int
zfs_vnop_exchange(ap)
	struct vnop_exchange_args /* {
		struct vnode *a_fvp;
		struct vnode *a_tvp;
		int a_options;
		vfs_context_t a_context;
	} */ *ap;
{

	return (0);
}

static int
zfs_vnop_revoke(ap)
	struct vnop_revoke_args /* {
		struct vnode *a_vp;
		int a_flags;
		vfs_context_t a_context;
	} */ *ap;
{

	return (vn_revoke(ap->a_vp, ap->a_flags, ap->a_context));
}

static int
zfs_vnop_blktooff(ap)
	struct vnop_blktooff_args /* {
		struct vnode *a_vp;
		daddr64_t a_lblkno;
		off_t *a_offset;
	} */ *ap;
{

	return (0);
}

static int
zfs_vnop_offtoblk(ap)
	struct vnop_offtoblk_args /* {
		struct vnode *a_vp;
		off_t a_offset;
		daddr64_t *a_lblkno;
	} */ *ap;
{

	return (0);
}

static int
zfs_vnop_blockmap(ap)
	struct vnop_blockmap_args /* {
		struct vnode *a_vp;
		off_t a_foffset;
		size_t a_size;
		daddr64_t *a_bpn;
		size_t *a_run;
		void *a_poff;
		int a_flags;
	} */ *ap;
{

	return (0);
}

static int
zfs_vnop_strategy(ap)
	struct vnop_strategy_args /* {
		struct buf *a_bp;
	} */ *ap;
{

	return (0);
}

static int
zfs_vnop_select(ap)
	struct vnop_select_args /* {
		struct vnode *a_vp;
		int a_which;
		int a_fflags;
		kauth_cred_t a_cred;
		void *a_wql;
		struct proc *a_p;
	} */ *ap;
{

	return (0);
}

static int
zfs_vnop_readdirattr(ap)
	struct vnop_readdirattr_args /* {
		struct vnodeop_desc *a_desc;
		struct vnode *a_vp;
		struct attrlist *a_alist;
		struct uio *a_uio;
		u_long a_maxcount;
		u_long a_options;
		u_long *a_newstate;
		int *a_eofflag;
		u_long *a_actualcount;
		vfs_context_t a_context;
	} */ *ap;
{

	return (0);
}

/* Directory vnode operations template */
int (**zfs_dvnodeops) (void *);
struct vnodeopv_entry_desc zfs_dvnodeops_template[] = {
	{&vnop_default_desc, 	(VOPFUNC)vn_default_error },
	{&vnop_lookup_desc,	(VOPFUNC)zfs_vnop_lookup},
	{&vnop_create_desc,	(VOPFUNC)zfs_vnop_create},
	{&vnop_whiteout_desc,	(VOPFUNC)zfs_vnop_whiteout},
	{&vnop_mknod_desc,	(VOPFUNC)zfs_vnop_mknod},
	{&vnop_open_desc,	(VOPFUNC)zfs_vnop_open},
	{&vnop_close_desc,	(VOPFUNC)zfs_vnop_close},
	{&vnop_access_desc,	(VOPFUNC)zfs_vnop_access},
	{&vnop_getattr_desc,	(VOPFUNC)zfs_vnop_getattr},
	{&vnop_setattr_desc,	(VOPFUNC)zfs_vnop_setattr},
	{&vnop_read_desc,	(VOPFUNC)zfs_isdir},
	{&vnop_write_desc,	(VOPFUNC)zfs_isdir},
	{&vnop_ioctl_desc,	(VOPFUNC)zfs_vnop_ioctl},
	{&vnop_select_desc,	(VOPFUNC)zfs_isdir},
	{&vnop_fsync_desc,	(VOPFUNC)zfs_vnop_fsync},
	{&vnop_remove_desc,	(VOPFUNC)zfs_vnop_remove},
	{&vnop_link_desc,	(VOPFUNC)zfs_vnop_link},
	{&vnop_rename_desc,	(VOPFUNC)zfs_vnop_rename},
	{&vnop_mkdir_desc,	(VOPFUNC)zfs_vnop_mkdir},
	{&vnop_rmdir_desc,	(VOPFUNC)zfs_vnop_rmdir},
	{&vnop_symlink_desc,	(VOPFUNC)zfs_vnop_symlink},
	{&vnop_readdir_desc,	(VOPFUNC)zfs_vnop_readdir},
	{&vnop_inactive_desc,	(VOPFUNC)zfs_vnop_inactive},
	{&vnop_reclaim_desc,	(VOPFUNC)zfs_vnop_reclaim},
	{&vnop_pathconf_desc,	(VOPFUNC)zfs_vnop_pathconf},
	{&vnop_revoke_desc,	(VOPFUNC)zfs_vnop_revoke},
	{&vnop_getxattr_desc,	(VOPFUNC)zfs_vnop_getxattr},
	{&vnop_setxattr_desc,	(VOPFUNC)zfs_vnop_setxattr},
	{&vnop_removexattr_desc,(VOPFUNC)zfs_vnop_removexattr},
	{&vnop_listxattr_desc,	(VOPFUNC)zfs_vnop_listxattr},
	{&vnop_readdirattr_desc, (VOPFUNC)zfs_vnop_readdirattr},
	{NULL, (VOPFUNC)NULL }
};
struct vnodeopv_desc zfs_dvnodeop_opv_desc =
{ &zfs_dvnodeops, zfs_dvnodeops_template };

/* Regular file vnode operations template */
int (**zfs_fvnodeops) (void *);
struct vnodeopv_entry_desc zfs_fvnodeops_template[] = {
	{&vnop_default_desc, 	(VOPFUNC)vn_default_error },
	{&vnop_whiteout_desc,	(VOPFUNC)zfs_vnop_whiteout},
	{&vnop_open_desc,	(VOPFUNC)zfs_vnop_open},
	{&vnop_close_desc,	(VOPFUNC)zfs_vnop_close},
	{&vnop_access_desc,	(VOPFUNC)zfs_vnop_access},
	{&vnop_getattr_desc,	(VOPFUNC)zfs_vnop_getattr},
	{&vnop_setattr_desc,	(VOPFUNC)zfs_vnop_setattr},
	{&vnop_read_desc,	(VOPFUNC)zfs_vnop_read},
	{&vnop_write_desc,	(VOPFUNC)zfs_vnop_write},
	{&vnop_ioctl_desc,	(VOPFUNC)zfs_vnop_ioctl},
	{&vnop_select_desc,	(VOPFUNC)zfs_vnop_select},
	{&vnop_fsync_desc,	(VOPFUNC)zfs_vnop_fsync},
	{&vnop_inactive_desc,	(VOPFUNC)zfs_vnop_inactive},
	{&vnop_reclaim_desc,	(VOPFUNC)zfs_vnop_reclaim},
	{&vnop_pathconf_desc,	(VOPFUNC)zfs_vnop_pathconf},
	{&vnop_pagein_desc,	(VOPFUNC)zfs_vnop_pagein},
	{&vnop_pageout_desc,	(VOPFUNC)zfs_vnop_pageout},
	{&vnop_mmap_desc,	(VOPFUNC)zfs_vnop_mmap},
	{&vnop_blktooff_desc,	(VOPFUNC)zfs_vnop_blktooff},
	{&vnop_offtoblk_desc,	(VOPFUNC)zfs_vnop_offtoblk},
	{&vnop_blockmap_desc,	(VOPFUNC)zfs_vnop_blockmap},
	{&vnop_strategy_desc,	(VOPFUNC)zfs_vnop_strategy},
	{&vnop_allocate_desc,   (VOPFUNC)zfs_vnop_allocate},
	{&vnop_revoke_desc,	(VOPFUNC)zfs_vnop_revoke},
	{&vnop_exchange_desc,	(VOPFUNC)zfs_vnop_exchange},
	{&vnop_getxattr_desc,	(VOPFUNC)zfs_vnop_getxattr},
	{&vnop_setxattr_desc,	(VOPFUNC)zfs_vnop_setxattr},
	{&vnop_removexattr_desc,(VOPFUNC)zfs_vnop_removexattr},
	{&vnop_listxattr_desc,	(VOPFUNC)zfs_vnop_listxattr},
#ifdef HAVE_NAMED_STREAMS
	{&vnop_getnamedstream_desc,	(VOPFUNC)zfs_vnop_getnamedstream},
	{&vnop_makenamedstream_desc,	(VOPFUNC)zfs_vnop_makenamedstream},
	{&vnop_removenamedstream_desc,	(VOPFUNC)zfs_vnop_removenamedstream},
#endif
	{NULL, (VOPFUNC)NULL }
};
struct vnodeopv_desc zfs_fvnodeop_opv_desc =
{ &zfs_fvnodeops, zfs_fvnodeops_template };

/* Symbolic link vnode operations template */
int (**zfs_symvnodeops) (void *);
struct vnodeopv_entry_desc zfs_symvnodeops_template[] = {
	{&vnop_default_desc, 	(VOPFUNC)vn_default_error },
	{&vnop_open_desc,	(VOPFUNC)zfs_vnop_open},
	{&vnop_close_desc,	(VOPFUNC)zfs_vnop_close},
	{&vnop_access_desc,	(VOPFUNC)zfs_vnop_access},
	{&vnop_getattr_desc,	(VOPFUNC)zfs_vnop_getattr},
	{&vnop_setattr_desc,	(VOPFUNC)zfs_vnop_setattr},
	{&vnop_ioctl_desc,	(VOPFUNC)zfs_vnop_ioctl},
	{&vnop_readlink_desc,	(VOPFUNC)zfs_vnop_readlink},
	{&vnop_inactive_desc,	(VOPFUNC)zfs_vnop_inactive},
	{&vnop_reclaim_desc,	(VOPFUNC)zfs_vnop_reclaim},
	{&vnop_pathconf_desc,	(VOPFUNC)zfs_vnop_pathconf},
	{&vnop_revoke_desc,	(VOPFUNC)zfs_vnop_revoke},
	{&vnop_getxattr_desc,	(VOPFUNC)zfs_vnop_getxattr},
	{&vnop_setxattr_desc,	(VOPFUNC)zfs_vnop_setxattr},
	{&vnop_removexattr_desc,(VOPFUNC)zfs_vnop_removexattr},
	{&vnop_listxattr_desc,	(VOPFUNC)zfs_vnop_listxattr},
	{NULL, (VOPFUNC)NULL }
};
struct vnodeopv_desc zfs_symvnodeop_opv_desc =
{ &zfs_symvnodeops, zfs_symvnodeops_template };

/* Extended attribtue directory vnode operations template */
int (**zfs_xdvnodeops) (void *);
struct vnodeopv_entry_desc zfs_xdvnodeops_template[] = {
	{&vnop_default_desc, 	(VOPFUNC)vn_default_error },
	{&vnop_lookup_desc,	(VOPFUNC)zfs_vnop_lookup},
	{&vnop_create_desc,	(VOPFUNC)zfs_vnop_create},
	{&vnop_whiteout_desc,	(VOPFUNC)zfs_vnop_whiteout},
	{&vnop_mknod_desc,	(VOPFUNC)zfs_inval},
	{&vnop_open_desc,	(VOPFUNC)zfs_vnop_open},
	{&vnop_close_desc,	(VOPFUNC)zfs_vnop_close},
	{&vnop_access_desc,	(VOPFUNC)zfs_vnop_access},
	{&vnop_getattr_desc,	(VOPFUNC)zfs_vnop_getattr},
	{&vnop_setattr_desc,	(VOPFUNC)zfs_vnop_setattr},
	{&vnop_read_desc,	(VOPFUNC)zfs_vnop_read},
	{&vnop_write_desc,	(VOPFUNC)zfs_vnop_write},
	{&vnop_ioctl_desc,	(VOPFUNC)zfs_vnop_ioctl},
	{&vnop_select_desc,	(VOPFUNC)zfs_vnop_select},
	{&vnop_fsync_desc,	(VOPFUNC)zfs_vnop_fsync},
	{&vnop_remove_desc,	(VOPFUNC)zfs_vnop_remove},
	{&vnop_link_desc,	(VOPFUNC)zfs_vnop_link},
	{&vnop_rename_desc,	(VOPFUNC)zfs_vnop_rename},
	{&vnop_mkdir_desc,	(VOPFUNC)zfs_inval},
	{&vnop_rmdir_desc,	(VOPFUNC)zfs_vnop_rmdir},
	{&vnop_symlink_desc,	(VOPFUNC)zfs_inval},
	{&vnop_readdir_desc,	(VOPFUNC)zfs_vnop_readdir},
	{&vnop_inactive_desc,	(VOPFUNC)zfs_vnop_inactive},
	{&vnop_reclaim_desc,	(VOPFUNC)zfs_vnop_reclaim},
	{&vnop_pathconf_desc,	(VOPFUNC)zfs_vnop_pathconf},
	{NULL, (VOPFUNC)NULL }
};
struct vnodeopv_desc zfs_xdvnodeop_opv_desc =
{ &zfs_xdvnodeops, zfs_xdvnodeops_template };

/* Error vnode operations template */
int (**zfs_evnodeops) (void *);
struct vnodeopv_entry_desc zfs_evnodeops_template[] = {
	{&vnop_default_desc, 	(VOPFUNC)vn_default_error },
	{&vnop_inactive_desc,	(VOPFUNC)zfs_vnop_inactive},
	{&vnop_reclaim_desc,	(VOPFUNC)zfs_vnop_reclaim},
	{&vnop_pathconf_desc,	(VOPFUNC)zfs_vnop_pathconf},
	{NULL, (VOPFUNC)NULL }
};
struct vnodeopv_desc zfs_evnodeop_opv_desc =
{ &zfs_evnodeops, zfs_evnodeops_template };
