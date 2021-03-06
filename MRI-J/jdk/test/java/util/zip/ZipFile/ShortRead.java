/*
 * Copyright 2005 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 */

/**
 * @test
 * @bug 6285124
 * @summary Small compressed zip entries should be read in one read() operation
 * @author Martin Buchholz
 */

import java.io.*;
import java.util.zip.*;

public class ShortRead {

    public static void main(String[] args) throws Exception {
        final File zFile = new File("abc.zip");
        try {
            final String entryName = "abc";
            final String data = "Data disponible";
            final ZipOutputStream zos =
                new ZipOutputStream(new FileOutputStream(zFile));
            zos.putNextEntry(new ZipEntry(entryName));
            zos.write(data.getBytes("ASCII"));
            zos.closeEntry();
            zos.close();

            final ZipFile zipFile = new ZipFile(zFile);
            final ZipEntry zentry = zipFile.getEntry(entryName);
            final InputStream inputStream = zipFile.getInputStream(zentry);
            System.out.printf("size=%d csize=%d available=%d%n",
                              zentry.getSize(),
                              zentry.getCompressedSize(),
                              inputStream.available());
            byte[] buf = new byte[data.length()];
            final int count = inputStream.read(buf);
            if (! new String(buf, "ASCII").equals(data) ||
                count != data.length())
                throw new Exception("short read?");
            zipFile.close();
        }
        finally { zFile.delete(); }
    }
}
