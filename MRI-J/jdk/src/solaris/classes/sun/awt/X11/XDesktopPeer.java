/*
 * Copyright 2005 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Sun designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Sun in the LICENSE file that accompanied this code.
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

package sun.awt.X11;


import java.io.File;
import java.io.IOException;
import java.net.MalformedURLException;
import java.net.URI;

import java.awt.Desktop.Action;
import java.awt.peer.DesktopPeer;


/**
 * Concrete implementation of the interface <code>DesktopPeer</code> for
 * the Gnome desktop on Linux and Unix platforms.
 *
 * @see DesktopPeer
 */
public class XDesktopPeer implements DesktopPeer {

    private static boolean nativeLibraryLoaded = false;
    static {
        nativeLibraryLoaded = init();
    }

    static boolean isDesktopSupported() {
        return nativeLibraryLoaded;
    }

    public boolean isSupported(Action type) {
        return type != Action.PRINT && type != Action.EDIT;
    }

    public void open(File file) throws IOException {
        try {
            launch(file.toURI());
        } catch (MalformedURLException e) {
            throw new IOException(file.toString());
        }
    }

    public void edit(File file) throws IOException {
        throw new UnsupportedOperationException("The current platform " +
            "doesn't support the EDIT action.");
    }

    public void print(File file) throws IOException {
        throw new UnsupportedOperationException("The current platform " +
            "doesn't support the PRINT action.");
    }

    public void mail(URI uri) throws IOException {
        launch(uri);
    }

    public void browse(URI uri) throws IOException {
        launch(uri);
    }

    private void launch(URI uri) throws IOException {
        if (!nativeLibraryLoaded) {
            throw new IOException("Failed to load native libraries.");
        }

        byte[] uriByteArray = ( uri.toString() + '\0' ).getBytes();
        boolean result = gnome_url_show(uriByteArray);
        if (!result) {
            throw new IOException("Failed to show URI:" + uri);
        }
    }

    private native boolean gnome_url_show(byte[] url);
    private static native boolean init();
}
