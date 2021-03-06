/*
 * Copyright 2002 Sun Microsystems, Inc.  All Rights Reserved.
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
 * @bug 4531817
 * @summary Inet[46]Address.localHost need doPrivileged
 * @run main/othervm GetLocalHostWithSM
 * files needed: GetLocalHostWithSM.java, MyPrincipal.java, and policy.file
 */

import java.net.*;

import javax.security.auth.Subject;
import java.security.Principal;
import java.security.PrivilegedExceptionAction;
import java.util.*;

public class GetLocalHostWithSM {

        public static void main(String[] args) throws Exception {

            // try setting the local hostname
            try {
                System.setProperty("host.name", InetAddress.
                                                getLocalHost().
                                                getHostName());
            } catch (UnknownHostException e) {
                System.out.println("Cannot find the local hostname, " +
                        "no nameserver entry found");
            }
            String policyFileName = System.getProperty("test.src", ".") +
                          "/" + "policy.file";
            System.setProperty("java.security.policy", policyFileName);
            System.setSecurityManager(new SecurityManager());

            InetAddress localHost1 = null;
            InetAddress localHost2 = null;

            localHost1 = InetAddress.getLocalHost();

            Subject mySubject = new Subject();
            MyPrincipal userPrincipal = new MyPrincipal("test");
            mySubject.getPrincipals().add(userPrincipal);
            localHost2 = (InetAddress)Subject.doAsPrivileged(mySubject,
                                new MyAction(), null);

            if (localHost1.equals(localHost2)) {
                throw new RuntimeException("InetAddress.getLocalHost() test " +
                                           " fails. localHost2 should be " +
                                           " the real address instead of " +
                                           " the loopback address."+localHost2);
            }
        }
}


class MyAction implements PrivilegedExceptionAction {

    public Object run() throws Exception {
        return InetAddress.getLocalHost();
    }
}
