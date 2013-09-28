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

static ssize_t fs2_read_file(struct file *filp, char *buf,
		size_t count, loff_t *offset)
{
	loff_t token_index;
	struct token *t;
	struct file_contents *file_content;
	size_t size_to_copy = 0;

	if(*offset > 0) {
		return 0;
	}

	file_content = (struct file_contents *) filp->private_data;

	if(!file_content)
		return -EFAULT;

	token_index = file_content->curr_position;
	file_content->curr_position += 1;

	list_for_each_entry(t, &(file_content->conts->list), list) {
		if(token_index-- == 0)
		{
			size_to_copy = (t->text_len < count)?t->text_len:count;
			if(copy_to_user(buf, t->text, size_to_copy))
			{
				return -EFAULT;
			}
			break;
		}
	}

	if(size_to_copy != 0)
		*offset += 1;

	return size_to_copy;
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
			new_token->text_len = last_word_len+2;
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
		inode->i_fop = &simple_dir_operations;
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
	inode->i_fop = &simple_dir_operations;

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
	inode->i_fop = &simple_dir_operations;

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

	return register_filesystem(&fs2_type);
}

static void __exit fs2_exit(void)
{
	unregister_filesystem(&fs2_type);
}

module_init(fs2_init);
module_exit(fs2_exit);

