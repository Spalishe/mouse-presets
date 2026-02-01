#include "main.h"
#include "render.h"
#include "wayland.h"
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
void *keys(void *arg) {
  struct input_event ev;
  int fd = *(int *)arg;
  while (true) {
    int64_t s = read(fd, &ev, sizeof(ev));
    if (s > 0) {
      if (ev.type != EV_KEY) {
        continue;
      }
      if (ev.value == 1) {
        if (ev.code == profile_switch_button) {
          update_text("test");
        }
      }
    }
  }
  return NULL;
}

int main(int argc, char *argv[]) {
  wayland_backend();

  char path[1024];
  char full_path[1024];
  bool found = false;
  DIR *dir = opendir("/dev/input/by-id/");
  struct dirent *hFile;
  if (dir) {
    while ((hFile = readdir(dir)) != NULL) {
      if (strcmp(hFile->d_name, mouse_name))
        continue;

      found = true;
      snprintf(full_path, sizeof(full_path), "/dev/input/by-id/%s",
               hFile->d_name);

      ssize_t len = readlink(full_path, path, sizeof(path) - 1);
      path[len] = '\0';
    }
    closedir(dir);
  } else
    return 1;

  if (!found) {
    printf("Not found event mouse!");
    return 1;
  }

  char new_path[1024];
  snprintf(new_path, sizeof(new_path), "/dev/input/by-id/%s", path);

  int fd = open(new_path, O_RDONLY);

  ioctl(fd, EVIOCGRAB, 1);
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);

  pthread_t thread1;
  pthread_create(&thread1, NULL, keys, (void *)&fd);
  while (true) {
    wayland_dispatch();
  }
  ioctl(fd, EVIOCGRAB, 0);
  close(fd);

  return 0;
}
