// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fuzz

import (
	"io"

	"github.com/golang/glog"
)

// This is similar to io.MultiReader but works in parallel instead of
// serial. This means ordering is not guaranteed, but this is no different
// than what would happen if we redirected stderr to stdout in any other
// way.
// TODO(fxbug.dev/106110): Once we are using only exec.Cmd and not ssh.Session
// we may be able to simply assign the same pipe object to Stdout and Stderr,
// but this does not work for ssh.Session (see comments about KillCloser)
func parallelMultiReader(readers ...io.Reader) io.ReadCloser {
	r, w := io.Pipe()

	errChs := make([]chan error, len(readers))
	for j, reader := range readers {
		errChs[j] = make(chan error, 1)
		go func(errCh chan<- error, r io.Reader) {
			_, err := io.Copy(w, r)
			errCh <- err
		}(errChs[j], reader)
	}

	go func() {
		// After all readers have stopped, propagate EOF.
		for j, errCh := range errChs {
			if err := <-errCh; err != nil {
				glog.Warningf("Error copying from reader %d: %s", j, err)
			}
		}
		w.Close()
	}()

	return r
}
