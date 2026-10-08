/* SPDX-License-Identifier: GPL-2.0 */
/* Internal bus adapters. Include after linux/pci.h and linux/acpi.h.
 * The offline test supplies API doubles and exercises these same functions. */
#ifndef OCTOOL_BUS_ACCESS_H
#define OCTOOL_BUS_ACCESS_H

static int octool_pci_access(u64 bus, u64 dev, u64 fn, u64 off, u64 width,
			     u64 value, bool writing, u64 *result)
{
	struct pci_dev *pdev;
	u8 byte = 0;
	u16 word = 0;
	u32 dword = 0;
	int rc;

	/* Validate before narrowing any field of the 96-byte request. */
	if (bus > 255 || dev > 31 || fn > 7 || off > 255 ||
	    (width != 1 && width != 2 && width != 4) || (off & (width - 1)))
		return -EINVAL;
	pdev = pci_get_domain_bus_and_slot(0, (unsigned int)bus,
					 PCI_DEVFN(dev, fn));
	if (!pdev)
		return -ENODEV;
	/* Respect config blockers (e.g. reset), without waiting indefinitely for
	 * them. The public accessors below use the platform's config mechanism
	 * and its locks; a module-local CF8/CFC lock cannot provide this. */
	if (!pci_cfg_access_trylock(pdev)) {
		pci_dev_put(pdev);
		return -EBUSY;
	}
	if (writing) {
		switch (width) {
		case 1: rc = pci_write_config_byte(pdev, off, (u8)value); break;
		case 2: rc = pci_write_config_word(pdev, off, (u16)value); break;
		default: rc = pci_write_config_dword(pdev, off, (u32)value); break;
		}
	} else {
		switch (width) {
		case 1: rc = pci_read_config_byte(pdev, off, &byte); dword = byte; break;
		case 2: rc = pci_read_config_word(pdev, off, &word); dword = word; break;
		default: rc = pci_read_config_dword(pdev, off, &dword); break;
		}
	}
	rc = pcibios_err_to_errno(rc);
	pci_cfg_access_unlock(pdev);
	pci_dev_put(pdev);
	if (!rc && !writing)
		*result = dword;
	return rc;
}

static int octool_ec_access(u64 index, u64 value, bool writing, u64 *result)
{
	if (index > 255 || (writing && value > 255))
		return -EINVAL;
#if IS_ENABLED(CONFIG_ACPI)
	{
		u8 byte = 0;
		int rc;

		/* These APIs address the ACPI driver's first EC, with its transaction
		 * mutex and firmware global lock where required. No fixed-port fallback
		 * and no inference about a board's registers or their units. */
		if (writing)
			return ec_write((u8)index, (u8)value);
		rc = ec_read((u8)index, &byte);
		if (!rc)
			*result = byte;
		return rc;
	}
#else
	(void)result;
	return -EOPNOTSUPP;
#endif
}

#endif
