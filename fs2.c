/*
 *
 * Copyright 2013, Fernando J. V. da Silva;
 * This file may be redistributed under the terms of the GNU GPL.
 *
 * Chances are that this code will crash your system, delete your
 * nethack high scores, and set your disk drives on fire.  You have
 * been warned.
 */
#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/vfs.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <asm/current.h>
#include <asm/uaccess.h>


/*
 * Boilerplate stuff.
 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Fernando J. V. da Silva");

static int inode_number = 0;

struct token {
	struct list_head list;
	char * text;
	size_t text_len;
};

struct file_contents {
	struct list_head list;
	loff_t curr_position;
	struct inode *inode;
	struct token *conts;
};



static int fs2_open(struct inode *inode, struct file *filp)
{
	if(inode->i_private)
		filp->private_data = inode->i_private;

	return 0;
}

static ssize_t fs2_read_word_from_file(struct file *filp, char *buf,
		size_t count, loff_t *offset, int to_user)
{
	struct token *t;
	loff_t token_index;
	struct file_contents *file_content;
	size_t size_to_copy = 0;

	file_content = (struct file_contents *) filp->private_data;
	token_index = *offset;

	if(!file_content)
		return -EFAULT;

	list_for_each_entry(t, &(file_content->conts->list), list) {
		if(token_index-- == 0)
		{
			size_to_copy = (t->text_len < count)?t->text_len:count;
			if(to_user) {
				if(copy_to_user(buf, t->text, size_to_copy))
				{
					return -EFAULT;
				}
			} else {
				size_t i;
				for(i=0; i<size_to_copy; i++) {
					buf[i] = t->text[i];
				}
			}
			break;
		}
	}

	if(size_to_copy != 0)
		*offset += 1;

	if(token_index > 0)
		return 0;

	return size_to_copy;
}

static ssize_t fs2_read_sentence_from_file(struct file *filp, char *buf,
		size_t count, int to_user)
{
	loff_t	offset = 0;
	size_t	bytes_read = 0;
	size_t	curr_bytes_read = 0;

	while((curr_bytes_read = fs2_read_word_from_file(filp,
													buf+bytes_read,
													(count-bytes_read)-2,
													&offset,
													to_user)) != 0)
	{
		bytes_read += curr_bytes_read;

		// remove \n after each word
		buf[bytes_read-1] = ' ';
	}

	// Add a '. ' into the end of the sentence
	buf[bytes_read-1] = '.';
	buf[bytes_read] = ' ';

	return (bytes_read+1);
}


static ssize_t fs2_read_file(struct file *filp, char *buf,
		size_t count, loff_t *offset)
{
	loff_t token_index;
	struct file_contents *file_content;
	ssize_t bytes_read = 0;

	if(*offset > 0) {
		return 0;
	}

	file_content = (struct file_contents *) filp->private_data;

	token_index = file_content->curr_position;
	file_content->curr_position += 1;

	bytes_read = fs2_read_word_from_file(filp, buf, count, &token_index, 1);

	*offset = token_index;

	return bytes_read;

}

#define IS_WORD_DELIMITER(chr) (chr == ' ' || chr == '\n' || chr == '\t')

static ssize_t fs2_write_file(struct file *filp, const char *buf,
		size_t count, loff_t *offset)
{
	struct file_contents *file_content;
	struct token *t;
	struct token *new_token;
	loff_t token_index = *offset;
	size_t i;
	const char *last_word;
	size_t last_word_len;
	struct inode *inode = filp->f_path.dentry->d_inode;

	file_content = (struct file_contents *) filp->private_data;

	list_for_each_entry(t, &(file_content->conts->list), list) {
		if(token_index-- == 0)
			break;
	}

	/* Add words after the token with the required offset */
	last_word = NULL;
	last_word_len = 0;
	for(i=0; i<count && buf[i] != '\0'; ++i)
	{
		if(IS_WORD_DELIMITER(buf[i]) && last_word != NULL)
		{
			new_token = kmalloc(sizeof(*new_token), GFP_KERNEL);
			new_token->text = kmalloc(last_word_len+2, GFP_KERNEL);
			if (copy_from_user(new_token->text, last_word, last_word_len))
				return -EFAULT;
			new_token->text[last_word_len] = '\n';
			new_token->text[last_word_len+1] = '\0';
			new_token->text_len = last_word_len+1;
			list_add(&(new_token->list), &(t->list));
			t = new_token;
			last_word = NULL;
			last_word_len = 0;

			inode->i_size++;
		} else {
			if(last_word == NULL)
			{
				last_word = buf + i;
			}
			last_word_len++;
		}
	}

	return count;
}

static inline int simple_positive(struct dentry *dentry)
{
	return dentry->d_inode && !d_unhashed(dentry);
}

static inline unsigned char dt_type(struct inode *inode)
{
	return (inode->i_mode >> 12) & 15;
}

static ssize_t fs2_get_inode_sentence(char *buf, size_t count, struct dentry *dentry)
{
	size_t	bytes_read = 0;
	size_t	curr_bytes_read;
	struct inode *inode = dentry->d_inode;
	struct file filp;
	struct list_head *p, *q;

	if(inode->i_mode & S_IFDIR)
	{
#if 0
		// It is not working recursively...
		p = q = &dentry->d_u.d_child;
		for (p=q->next; p != &dentry->d_subdirs; p=p->next) {
			struct dentry *next;
			next = list_entry(p, struct dentry, d_u.d_child);

			if (!simple_positive(next)) {
				continue;
			}

			if(next == dentry) {
				return;
			}

			curr_bytes_read = fs2_get_inode_sentence(buf+bytes_read, (count-bytes_read)-2, next);

			if(!curr_bytes_read)
				return -EFAULT;

			bytes_read += curr_bytes_read;
		}

		// End the paragraph
		buf[bytes_read] = '\n';
		buf[bytes_read+1] = '\n';
#endif
	}
	else
	{
		fs2_open(inode, &filp);

		curr_bytes_read = fs2_read_sentence_from_file(&filp, buf, count, 0);

		if(!curr_bytes_read)
			return -EFAULT;

		bytes_read += curr_bytes_read;
	}

	return bytes_read;
}

#define READDIR_BUF_SIZE	1024

int fs2_readdir( struct file *filp, void *dirent, filldir_t filldir )
{
	struct dentry *dentry = filp->f_path.dentry;
	struct dentry *cursor = filp->private_data;
	struct list_head *p, *q = &cursor->d_u.d_child;
	ino_t ino;
	int i = filp->f_pos;
	int j;
	char	buff[READDIR_BUF_SIZE];
	ssize_t	bytes_read;

	for(j=0; j<READDIR_BUF_SIZE; j++) {
		buff[j] = 0;
	}

	switch (i) {
			case 0:
				filp->f_pos++;
				i++;
				/* fallthrough */
			case 1:
				filp->f_pos++;
				i++;
				/* fallthrough */
		default:
			if (filp->f_pos == 2)
				list_move(q, &dentry->d_subdirs);

			for (p=q->next; p != &dentry->d_subdirs; p=p->next) {
				struct dentry *next;
				next = list_entry(p, struct dentry, d_u.d_child);
				if (!simple_positive(next)) {
					continue;
				}

				if(next->d_inode->i_mode & S_IFDIR) {
					continue;
				}

				bytes_read = fs2_get_inode_sentence(buff, READDIR_BUF_SIZE, next);

				if(!bytes_read)
					return -EFAULT;

				if (filldir(dirent, buff,
						bytes_read, filp->f_pos,
						next->d_inode->i_ino,
						dt_type(next->d_inode)) < 0)
					return 0;

				/* next is still alive */
				list_move(q, p);
				p = q;
				filp->f_pos++;
			}
	}
	return 0;

}


/*
 * Now we can put together our file operations structure.
 */
static struct file_operations fs2_file_ops = {
	.open	= fs2_open,
	.read 	= fs2_read_file,
	.write  = fs2_write_file,
};

static const struct inode_operations fs2_inode_ops = {
	.getattr        = simple_getattr,
};

static struct inode_operations fs2_dir_inode_operations;
static struct file_operations fs2_dir_operations;

static int fs2_create (struct inode *dir, struct dentry * dentry,
			    int mode, struct nameidata *nd)
{
	struct inode *inode;
	struct file_contents *file = kmalloc(sizeof(*file), GFP_KERNEL);
	if (!file)
		return -EAGAIN;

	inode = new_inode(dir->i_sb);

	inode->i_mode = mode | S_IFREG;
	inode->i_uid = current->cred->fsuid;
	inode->i_gid = current->cred->fsgid;
	inode->i_blocks = 0;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_ino = ++inode_number;

	switch(mode & S_IFMT) {
	case S_IFREG:
		inode->i_op = &fs2_inode_ops;
		inode->i_fop = &fs2_file_ops;
		break;
	case S_IFDIR:
		inode->i_op = &fs2_dir_inode_operations;
		inode->i_fop = &fs2_dir_operations;
		break;
	}

	file->inode = inode;
	file->conts = kmalloc(sizeof(struct token), GFP_KERNEL);
	file->curr_position = 0;
	if (!file->conts)
		return -EAGAIN;

	inode->i_private = file;

	INIT_LIST_HEAD(&file->conts->list);


	d_instantiate(dentry, inode);
	dget(dentry);

	return 0;
}

static int fs2_mkdir(struct inode * dir, struct dentry *dentry,
						int mode)
{
	struct inode *inode;

	inode = new_inode(dir->i_sb);

	if(!inode) {
		return -ENOMEM;
	}

	inode->i_mode = mode | S_IFDIR;
	inode->i_uid = current->cred->fsuid;
	inode->i_gid = current->cred->fsgid;
	inode->i_blocks = 0;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_ino = ++inode_number;

	inode->i_op = &fs2_dir_inode_operations;
	inode->i_fop = &fs2_dir_operations;

	inode->i_private = NULL;
	inc_nlink(inode);

	if (dir->i_mode & S_ISGID) {
		inode->i_gid = dir->i_gid;
		if (S_ISDIR(mode))
			inode->i_mode |= S_ISGID;
	}

	d_instantiate(dentry, inode);
	dget(dentry);
	dir->i_mtime = dir->i_ctime = CURRENT_TIME;
	inode->i_ino = ++inode_number;
	inc_nlink(dir);

	return 0;

}

const struct super_operations fs2_ops = {
    .statfs		= simple_statfs,
    .drop_inode	= generic_delete_inode,
};


static int fs2_fill_super (struct super_block *sb, void *data, int silent)
{
	struct inode * inode;
	struct dentry * root;

	sb->s_maxbytes = 4096;
	sb->s_magic = 0xF5F5F5;
	sb->s_blocksize = 1024;
	sb->s_blocksize_bits = 10;

    sb->s_op = (struct super_operations *) &fs2_ops;
    sb->s_time_gran = 1;

    inode = new_inode(sb);

    if (!inode)
    	return -ENOMEM;

    inode->i_ino = ++inode_number;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	inode->i_blocks = 0;
	inode->i_uid = inode->i_gid = 0;
	inode->i_mode = S_IFDIR | S_IRUGO | S_IXUGO | S_IWUSR;
	inode->i_op = &fs2_dir_inode_operations;
	inode->i_fop = &fs2_dir_operations;


	root = d_make_root(inode);
	if (!root) {
		iput(inode);
		return -ENOMEM;
	}
	sb->s_root = root;
	return 0;

}


static struct dentry *fs2_get_super(struct file_system_type *fst,
		int flags, const char *devname, void *data)
{
	return mount_single(fst, flags, data, fs2_fill_super);
}

static struct file_system_type fs2_type = {
	.owner 		= THIS_MODULE,
	.name		= "fs2",
	.mount		= fs2_get_super,
	.kill_sb	= kill_litter_super,
};

static int __init fs2_init(void)
{
	fs2_dir_inode_operations.create = fs2_create;
	fs2_dir_inode_operations.lookup = simple_lookup;
	fs2_dir_inode_operations.mkdir	= fs2_mkdir;

	fs2_dir_operations.open = simple_dir_operations.open;
	fs2_dir_operations.release = simple_dir_operations.release;
	fs2_dir_operations.llseek = simple_dir_operations.llseek;
	fs2_dir_operations.read	= simple_dir_operations.read;
	fs2_dir_operations.fsync = simple_dir_operations.fsync;
	fs2_dir_operations.readdir = fs2_readdir;

	return register_filesystem(&fs2_type);
}

static void __exit fs2_exit(void)
{
	unregister_filesystem(&fs2_type);
}

module_init(fs2_init);
module_exit(fs2_exit);

