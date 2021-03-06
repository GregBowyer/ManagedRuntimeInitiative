/*
 * Copyright 2003 Sun Microsystems, Inc.  All Rights Reserved.
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
 * @bug 4963416
 * @summary make sure removeProvider() always works correctly
 * @author Andreas Sterbenz
 */

import java.util.*;

import java.security.*;

public class RemoveProviders {

    public static void main(String[] args) throws Exception {
        Provider[] providers = Security.getProviders();
        System.out.println("Providers: " + Arrays.asList(providers));

        // remove each provider and add it back in at the end of the list
        for (int i = 0; i < providers.length; i++) {
            Provider p = providers[i];
            String name = p.getName();
            Security.removeProvider(name);
            if (Security.getProvider(name) != null) {
                throw new Exception("Provider not removed: " + name);
            }
            Security.addProvider(p);
        }
        if (Arrays.equals(providers, Security.getProviders()) == false) {
            throw new Exception("Provider mismatch: " + Arrays.asList(Security.getProviders()));
        }

        // remove non-existing provider
        Security.removeProvider("foo");
        if (Arrays.equals(providers, Security.getProviders()) == false) {
            throw new Exception("Provider mismatch: " + Arrays.asList(Security.getProviders()));
        }

        // remove from the middle of the list
        Security.removeProvider("SunJCE");
        if (Security.getProvider("SunJCE") != null) {
            throw new Exception("not removed");
        }

        System.out.println("Done.");
    }

}
