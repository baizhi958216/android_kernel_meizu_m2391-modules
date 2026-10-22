/* SPDX-License-Identifier: GPL-2.0-only */
/* Both amplifiers may name the same physical reset/switch line. */
struct cs35l45_gpio_owner {
	struct list_head entry;
	int gpio;
	unsigned int users;
	bool reset;
};
static LIST_HEAD(cs35l45_gpio_owners);
static DEFINE_MUTEX(cs35l45_gpio_lock);

static int cs35l45_gpio_get(int gpio, bool reset)
{
	struct cs35l45_gpio_owner *owner;
	int ret = 0;

	mutex_lock(&cs35l45_gpio_lock);
	list_for_each_entry(owner, &cs35l45_gpio_owners, entry) {
		if (owner->gpio != gpio)
			continue;
		if (owner->reset != reset) {
			ret = -EINVAL;
			goto out;
		}
		owner->users++;
		goto out;
	}
	owner = kzalloc(sizeof(*owner), GFP_KERNEL);
	if (!owner) {
		ret = -ENOMEM;
		goto out;
	}
	ret = gpio_request_one(gpio, GPIOF_OUT_INIT_LOW,
		reset ? "cs35l45 reset" : "cs35l45 switch");
	if (ret) {
		kfree(owner);
		goto out;
	}
	if (reset) {
		usleep_range(2000, 2100);
		ret = gpio_direction_output(gpio, 1);
		if (ret) {
			gpio_free(gpio);
			kfree(owner);
			goto out;
		}
	}
	owner->gpio = gpio;
	owner->users = 1;
	owner->reset = reset;
	list_add(&owner->entry, &cs35l45_gpio_owners);
out:
	mutex_unlock(&cs35l45_gpio_lock);
	return ret;
}

static void cs35l45_gpio_put(int gpio)
{
	struct cs35l45_gpio_owner *owner;

	mutex_lock(&cs35l45_gpio_lock);
	list_for_each_entry(owner, &cs35l45_gpio_owners, entry) {
		if (owner->gpio != gpio)
			continue;
		if (--owner->users)
			break;
		gpio_set_value_cansleep(gpio, 0);
		gpio_free(gpio);
		list_del(&owner->entry);
		kfree(owner);
		break;
	}
	mutex_unlock(&cs35l45_gpio_lock);
}
